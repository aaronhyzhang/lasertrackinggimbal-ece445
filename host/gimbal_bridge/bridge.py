"""Laptop -> PCB tracking bridge for the dual-axis PCB_CONTROL firmware.

Vision code calls `Bridge.submit(Observation(...))` at frame rate. A background
loop turns the latest observation into absolute G1 POSITION targets for tilt
(UDP 5005) and pan (UDP 5006), keeps both 500 ms leases alive and polls STATUS.

Safety behaviour (unchanged firmware rules apply on top of these):
  * start() requires both axes READY, calibrated and zeroed, and first commands
    each axis to its CURRENT measured position, so pressing S1 causes no jump.
  * Arming is only the physical S1 button.
  * Target lost / stale vision -> hold the last target (SEARCHING), keep leases.
  * Firmware with the system state machine (STATUS has sys=) also receives
    `TARGET <0|1>` every cycle on the tilt port, so the board knows TRACKING from SEARCHING.
  * Any fault, unexpected state, refused command or lost link -> DISABLE both and stop.
"""
import collections
import csv
import threading
import time
from dataclasses import dataclass

from .g1 import (G1Client, G1Error, DEFAULT_HOST, TILT_PORT, PAN_PORT,
                 SERVO_READY, SERVO_WAKE_RUN, SERVO_RUNNING, STATE_NAMES, FAULT_NAMES, SYSTEM_NAMES)
from .tracking import Observation, TrackerConfig, next_target

AXES = ('tilt', 'pan')  # index == firmware axis number
HEARTBEAT_S = 0.1       # well inside the 500 ms firmware lease
MAX_MISSED_REPLIES = 8  # consecutive timeouts on one axis before giving up


class BridgeError(RuntimeError):
    pass


@dataclass
class AxisState:
    measured_deg: float = 0.0
    target_deg: float = 0.0
    state: int = 0
    fault: int = 0
    last_command_s: float = 0.0
    missed: int = 0


class Bridge:
    def __init__(self, cfg=None, host=DEFAULT_HOST, tilt_port=TILT_PORT, pan_port=PAN_PORT,
                 rate_hz=50.0, timeout=0.05, log_path=None):
        self.cfg = cfg or TrackerConfig()
        self.cfg.validate()
        self.clients = [G1Client(host, tilt_port, timeout), G1Client(host, pan_port, timeout)]
        self.limits = [self.cfg.tilt_limits, self.cfg.pan_limits]
        self.signs = [self.cfg.tilt_sign, self.cfg.pan_sign]
        self.axes = [AxisState(), AxisState()]
        self.period = 1.0 / rate_hz
        self.mode = 'IDLE'
        self.system_state = None   # board's sys= value, if the firmware reports it
        self.send_target = False   # decided in check_ready() from the firmware's STATUS
        self.rtts = collections.deque(maxlen=20000)
        self._obs = None
        self._obs_new = False
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = None
        self.error = None
        self._log_file = open(log_path, 'w', newline='') if log_path else None
        self._log = csv.writer(self._log_file) if self._log_file else None
        if self._log:
            self._log.writerow(['t_s', 'mode', 'detected', 'pan_err_deg', 'tilt_err_deg',
                                'tilt_target', 'tilt_meas', 'tilt_state', 'pan_target', 'pan_meas',
                                'pan_state', 'rtt_ms', 'board_sys'])
        self._t0 = time.monotonic()

    # ---- vision side -------------------------------------------------------
    def submit(self, obs):
        """Thread-safe: hand the newest observation to the control loop."""
        if obs.t_capture is None:
            obs.t_capture = time.monotonic()
        with self._lock:
            self._obs = obs
            self._obs_new = True

    def positions(self):
        """Latest measured (tilt_deg, pan_deg); useful for simulators and displays."""
        return self.axes[0].measured_deg, self.axes[1].measured_deg

    # ---- board side --------------------------------------------------------
    def _status(self, axis):
        s = self.clients[axis].status()
        self.rtts.append(self.clients[axis].last_rtt)
        a = self.axes[axis]
        a.measured_deg = s['position_mdeg'] / 1000
        a.state, a.fault = s['state'], s['fault']
        if axis == 0 and 'sys' in s:
            self.system_state = s['sys']
        return s

    def check_ready(self):
        """Verify both axes run the dual Ethernet build and are calibrated, zeroed and READY."""
        problems = []
        for axis, name in enumerate(AXES):
            s = self._status(axis)
            if s.get('axis') != axis or s.get('dual') != 1 or s.get('source') != 1 or not s.get('enabled'):
                problems.append(f'{name}: not the dual Ethernet motion build ({s})')
            elif s['fault']:
                problems.append(f"{name}: fault {FAULT_NAMES.get(s['fault'], s['fault'])}")
            elif not (s['calibrated'] and s['zero']):
                problems.append(f'{name}: needs calibration and mechanical zero (Tools/calibrate_motor.py)')
            elif s['state'] != SERVO_READY:
                problems.append(f"{name}: state {STATE_NAMES.get(s['state'], s['state'])}, expected READY")
            else:
                low, high = self.limits[axis]
                if not low <= self.axes[axis].measured_deg <= high:
                    problems.append(f'{name}: at {self.axes[axis].measured_deg:.1f} deg, outside '
                                    f'tracking window {low}..{high}; move it inside first')
        if problems:
            raise BridgeError('; '.join(problems))
        self.send_target = self.system_state is not None

    def hold_current(self):
        """Queue each axis's current position as its target (S1 then arms without a jump)."""
        for axis in (0, 1):
            a = self.axes[axis]
            a.target_deg = a.measured_deg
            self.clients[axis].position(a.target_deg)
            a.last_command_s = time.monotonic()

    def wait_armed(self, timeout_s=60.0):
        """Keep leases alive until both axes report RUNNING (after the user presses S1)."""
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            states = []
            for axis in (0, 1):
                self.clients[axis].heartbeat()
                self._status(axis)
                if self.axes[axis].fault:
                    raise BridgeError(f'{AXES[axis]} fault {FAULT_NAMES.get(self.axes[axis].fault)}')
                states.append(self.axes[axis].state)
            if all(s == SERVO_RUNNING for s in states):
                self.mode = 'SEARCHING'
                return
            if any(s not in (SERVO_READY, SERVO_WAKE_RUN, SERVO_RUNNING) for s in states):
                raise BridgeError(f'unexpected states while arming: {states}')
            time.sleep(HEARTBEAT_S)
        raise BridgeError('S1 was not pressed (both axes not RUNNING) before timeout')

    def step(self):
        """One control cycle: update targets from vision, refresh leases, poll status."""
        now = time.monotonic()
        with self._lock:
            obs, fresh = self._obs, self._obs_new
            self._obs_new = False
        tracking = obs is not None and obs.detected and now - obs.t_capture <= self.cfg.stale_s
        self.mode = 'TRACKING' if tracking else 'SEARCHING'
        errors = (obs.tilt_error_deg, obs.pan_error_deg) if tracking else (0.0, 0.0)
        for axis in (0, 1):
            a = self.axes[axis]
            new = a.target_deg
            if tracking and fresh:
                new = next_target(a.measured_deg, errors[axis], self.signs[axis], self.cfg,
                                  self.limits[axis], a.target_deg)
            try:
                if abs(new - a.target_deg) >= self.cfg.min_update_deg:
                    self.clients[axis].position(new)
                    a.target_deg, a.last_command_s = new, now
                elif now - a.last_command_s >= HEARTBEAT_S:
                    self.clients[axis].heartbeat()
                    a.last_command_s = now
                if axis == 0 and self.send_target:
                    self.clients[0].target(tracking)
                self._status(axis)
                a.missed = 0
            except TimeoutError:
                a.missed += 1
                if a.missed >= MAX_MISSED_REPLIES:
                    raise BridgeError(f'{AXES[axis]}: {a.missed} replies missed; link lost')
                continue
            except G1Error as exc:
                raise BridgeError(f'{AXES[axis]} refused a command ({exc.reply}); '
                                  'lease expired, S2 pressed or the other axis stopped') from exc
            if a.fault or a.state != SERVO_RUNNING:
                raise BridgeError(f'{AXES[axis]} left RUNNING: state '
                                  f'{STATE_NAMES.get(a.state, a.state)} fault {FAULT_NAMES.get(a.fault, a.fault)}')
        if self._log:
            t, p = self.axes
            self._log.writerow([f'{now - self._t0:.4f}', self.mode, int(bool(obs and obs.detected)),
                                f'{obs.pan_error_deg:.3f}' if obs else '', f'{obs.tilt_error_deg:.3f}' if obs else '',
                                f'{t.target_deg:.3f}', f'{t.measured_deg:.3f}', t.state,
                                f'{p.target_deg:.3f}', f'{p.measured_deg:.3f}', p.state,
                                f'{self.rtts[-1] * 1000:.3f}' if self.rtts and self.rtts[-1] else '',
                                SYSTEM_NAMES.get(self.system_state, '')])

    def _run(self):
        next_tick = time.monotonic()
        try:
            while not self._stop.is_set():
                self.step()
                next_tick += self.period
                delay = next_tick - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
                else:
                    next_tick = time.monotonic()
        except BaseException as exc:  # noqa: BLE001 - stop motors on any failure
            self.error = exc
        finally:
            self.disable()

    def start(self, arm_timeout_s=60.0, on_waiting_for_s1=None):
        """check_ready -> hold_current -> wait for S1 -> run the loop in a background thread."""
        self.check_ready()
        try:
            self.hold_current()
            if on_waiting_for_s1:
                on_waiting_for_s1()
            self.wait_armed(arm_timeout_s)
        except BaseException:
            self.disable()
            raise
        self._thread = threading.Thread(target=self._run, name='gimbal-bridge', daemon=True)
        self._thread.start()

    def running(self):
        return self._thread is not None and self._thread.is_alive()

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)
        self.disable()

    def disable(self):
        """DISABLE both axes (either one already stops both). Never raises."""
        self.mode = 'IDLE'
        for client in self.clients:
            try:
                client.disable()
            except (OSError, RuntimeError, TimeoutError):
                pass

    def close(self):
        self.stop()
        for client in self.clients:
            client.close()
        if self._log_file:
            self._log_file.close()
