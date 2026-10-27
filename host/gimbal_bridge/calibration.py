"""Bring both axes to calibrated, zeroed READY before tracking.

The board restores calibration from flash at boot (cal_store.c), so usually there is
nothing to do. Otherwise, per axis and one axis at a time (the firmware requires it):

  1. Electrical calibration if needed: CALIBRATE, heartbeats while the shaft aligns,
     sweeps about 8 degrees and returns (~7 s on the real board).
  2. Mechanical zero if needed: HOME where the firmware supports it (pan: moves slowly
     to its clockwise hard stop after you press S1), otherwise you place the shaft at
     its 0-degree position by hand and press Enter.

Nothing arms position control. S2 stops everything at any time.

    py -3 -m gimbal_bridge.calibration            # check, and calibrate what is missing
    py -3 -m gimbal_bridge.calibration --forget   # erase stored calibration first
"""
import argparse
import time

from .g1 import (G1Client, G1Error, DEFAULT_HOST, TILT_PORT, PAN_PORT, SERVO_READY, SERVO_OFF,
                 SERVO_HOMING, SERVO_WAKE_RUN, SERVO_CALIBRATING, STATE_NAMES, FAULT_NAMES)

AXES = ('tilt', 'pan')
POLL_S = 0.08


class CalibrationError(RuntimeError):
    pass


class ConsoleUI:
    """Prompts on the terminal. Replace with a scripted object in tests or a GUI."""

    def info(self, message):
        print(message, flush=True)

    def confirm(self, message):
        input(message + ' ')

    def wait_for_s1(self, message):
        print(message, flush=True)


def describe(s):
    stored = {0: 'nothing', 1: 'electrical', 2: 'electrical + zero'}.get(s.get('stored'), '?')
    restored = {0: 'nothing', 1: 'electrical', 2: 'electrical + zero'}.get(s.get('restored'), '?')
    return (f"state {STATE_NAMES.get(s['state'], s['state'])}, calibrated={s['calibrated']} zero={s['zero']}, "
            f"flash has {stored}, restored at boot: {restored}")


def ready(s):
    return s['state'] == SERVO_READY and s['calibrated'] and s['zero'] and not s['fault']


def _check(name, s):
    if s['fault']:
        raise CalibrationError(f"{name}: fault {FAULT_NAMES.get(s['fault'], s['fault'])}; reset the board")
    if s.get('source') != 1 or not s.get('enabled'):
        raise CalibrationError(f'{name}: firmware is not the Ethernet motion build')


def _keep_alive(client):
    """Heartbeat; completion can end the lease between polls, so a refusal is not fatal here."""
    try:
        client.heartbeat()
    except G1Error:
        pass


def calibrate_axis(client, name, ui, timeout_s=20.0):
    ui.info(f'{name}: electrical calibration (the shaft will align, sweep ~8 degrees and return)...')
    client.calibrate()
    started, deadline = False, time.monotonic() + timeout_s
    try:
        while time.monotonic() < deadline:
            s = client.status()
            _check(name, s)
            started |= s['state'] in SERVO_CALIBRATING
            if started and s['state'] == SERVO_READY and s['calibrated']:
                ui.info(f'{name}: electrical calibration done')
                return
            if started and s['state'] not in SERVO_CALIBRATING:
                raise CalibrationError(f'{name}: calibration stopped ({describe(s)})')
            _keep_alive(client)
            time.sleep(POLL_S)
        raise CalibrationError(f'{name}: calibration timed out')
    except BaseException:
        _disable(client)
        raise


def zero_by_hand(client, name, ui):
    ui.confirm(f'{name}: move the shaft by hand to its 0-degree position, let go, then press Enter.')
    client.zero()
    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        s = client.status()
        _check(name, s)
        if s['zero']:
            ui.info(f'{name}: zero set')
            return
        time.sleep(POLL_S)
    raise CalibrationError(f'{name}: zero was not confirmed')


def home_axis(client, name, ui, s1_timeout_s=60.0, home_timeout_s=30.0):
    client.home()
    ui.wait_for_s1(f'{name}: press S1 on the board to home (slow move to the hard stop). S2 cancels.')
    started, deadline = False, time.monotonic() + s1_timeout_s
    try:
        while time.monotonic() < deadline:
            s = client.status()
            _check(name, s)
            if s['state'] in (SERVO_WAKE_RUN, SERVO_HOMING) and not started:
                started, deadline = True, time.monotonic() + home_timeout_s
            if started and s['state'] == SERVO_READY:
                if not s['zero']:
                    raise CalibrationError(f'{name}: homing ended without a zero')
                ui.info(f'{name}: homed, zero set at the hard stop')
                return
            _keep_alive(client)
            time.sleep(POLL_S)
        raise CalibrationError(f'{name}: ' + ('homing timed out' if started else 'S1 was not pressed'))
    except BaseException:
        _disable(client)
        raise


def _disable(client):
    try:
        client.disable()
    except (OSError, RuntimeError, TimeoutError):
        pass


def ensure_ready(clients, ui=None, allow_home=True):
    """Make both axes calibrated, zeroed and READY. Returns {'tilt': action, 'pan': action}."""
    ui = ui or ConsoleUI()
    actions = {}
    for axis, (client, name) in enumerate(zip(clients, AXES)):
        s = client.status()
        _check(name, s)
        if s.get('axis') != axis:
            raise CalibrationError(f'{name}: port answers as axis {s.get("axis")}')
        ui.info(f'{name}: {describe(s)}')
        if ready(s):
            actions[name] = 'restored' if s.get('restored') == 2 else 'already ready'
            continue
        if s['state'] not in (SERVO_OFF, SERVO_READY):
            raise CalibrationError(f'{name}: must be stopped (state {STATE_NAMES.get(s["state"])}); press S2')
        done = []
        if not s['calibrated']:
            calibrate_axis(client, name, ui)
            done.append('calibrated')
        if allow_home and s.get('home_supported') == 1:
            try:
                home_axis(client, name, ui)
                done.append('homed')
            except G1Error:
                ui.info(f'{name}: board refused HOME; falling back to setting zero by hand')
                zero_by_hand(client, name, ui)
                done.append('zeroed by hand')
        else:
            zero_by_hand(client, name, ui)
            done.append('zeroed by hand')
        actions[name] = ' + '.join(done)
    for client, name in zip(clients, AXES):
        s = client.status()
        if not ready(s):
            raise CalibrationError(f'{name}: not ready after calibration ({describe(s)})')
    return actions


def forget(clients, ui=None):
    ui = ui or ConsoleUI()
    clients[0].forget()
    ui.info('Stored calibration erased on the board (both axes).')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--host', default=DEFAULT_HOST)
    p.add_argument('--tilt-port', type=int, default=TILT_PORT)
    p.add_argument('--pan-port', type=int, default=PAN_PORT)
    p.add_argument('--forget', action='store_true', help='erase stored calibration, then calibrate')
    p.add_argument('--no-home', action='store_true', help='set every zero by hand')
    a = p.parse_args()
    clients = [G1Client(a.host, a.tilt_port, 0.2), G1Client(a.host, a.pan_port, 0.2)]
    try:
        if a.forget:
            forget(clients)
        actions = ensure_ready(clients, allow_home=not a.no_home)
        print('READY: ' + ', '.join(f'{k} {v}' for k, v in actions.items()))
    except (CalibrationError, G1Error, TimeoutError) as exc:
        print(f'FAIL: {exc}')
        raise SystemExit(1)
    except KeyboardInterrupt:
        print('Stopped.')
        raise SystemExit(130)
    finally:
        for c in clients:
            c.close()


if __name__ == '__main__':
    main()
