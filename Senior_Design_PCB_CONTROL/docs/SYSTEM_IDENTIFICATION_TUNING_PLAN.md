# Measured tuning and automated commissioning plan

Date: 2026-10-27. Status: researched proposal; no tuning motion run and no
controller changes made for this planning task. Preserve sinusoidal encoder-based
voltage commutation (CONTROL_CURRENT_PI_ENABLED=0). Tune one axis at a time,
with the final mechanism/representative load. User objective: within +/-1 degree
and stable, then maximize responsiveness, acceleration and speed inside one
bounded 0..360-degree turn.

## Findings from this project's code and logs

1. There is an actuator-limit mismatch. position_tick limits the velocity PI
   using CONTROL_RUN_VOLTS (currently 6 V), and decides whether to integrate from
   that saturation. position_fast later limits actual voltage to
   min(CONTROL_RUN_VOLTS, CONTROL_VOLT_LIMIT, bus_v*0.15), about 3.6 V at 24 V.
   The integrator can therefore keep accumulating while the downstream actuator
   is already clipped. This is a code-level anti-windup deficiency, not evidence
   that it caused every observed oscillation. Old logs do not contain the input
   voltage or integral state needed to establish that causal link.
2. Current telemetry is laptop-timestamped at approximately 12 Hz and contains
   position/RPM but no synchronized applied voltage, trajectory, controller
   terms, saturation, or firmware gain readback. It supports performance scoring,
   not reliable plant identification. Config labels are unverified local headers.
3. The existing tool's recommendations are hand-written heuristics. They are not
   measured stability margins, identified gains or an automatic optimizer.
4. position_20261027T025331_433603Z.json passed 12 moves with labels
   100 RPM, 75 RPM/s, position Kp=5, velocity Kp=0.5, Ki=0.5. Worst sampled
   overshoot was 10.715 degrees; longest move 12.227 s including dwell. It is a
   reproducible reference to investigate, not a +/-1-degree-qualified tune.
   My previous recollection of position Kp=4 as the proven baseline was not
   supported by this saved successful run.
5. position_20261027T025520_951186Z.json had the same labels, passed six moves,
   then timed out on target 92.805 degrees. The same labelled gains are not
   repeatably adequate at every angle/direction.
6. position_20261027T024043_343896Z.csv cycled roughly 82..96 degrees around 90
   with integral enabled. The integral-off trace 024421 stopped near 80.156
   degrees for a 90-degree command. These suggest nonlinear effects and/or loop
   problems; they do not uniquely identify friction, cogging, gravity or a
   commutation/encoder issue.
7. Trial 025041 faulted SERVO_TRAVEL before useful motion, from approximately
   -0.79 degrees at arming. This is a zero/travel failure, not gain-identification
   data or a heartbeat fault. Do not use it to update gains.
8. A read-only STATUS probe to 192.168.10.2 during this planning turn timed out.
   The architecture supports unattended batches once reachable, but live access
   must be verified before an actual session. No motion was commanded.

## Research conclusions

Tune cascaded loops from the inside outward: here voltage actuation/measurement
quality, then the velocity PI, then position P, then motion profiles. This does
not require switching to current FOC. [1,2]

Industrial autotuners inject defined signals, measure input and output, estimate
frequency response, derive gains for bandwidth/margin, and validate them. They
do not infer adequate gains from a single overshooting position step. [3]

Anti-windup must account for actual actuator saturation, including limits after
the PI. [4] Identification data must excite the dynamics above noise/friction;
highly nonlinear systems or restricted travel need adapted experiments rather
than blindly copying a free-rotation chirp. The cited vendor procedure explicitly
does not support limited-range mechanisms; use its principles, not its motion
sequence, for this bounded gimbal. [5]

Validate an identified model on different records, not only the fitting trace.
Poor holdout prediction means the derived gains are not yet trustworthy. [6]

## Phase A: make experiments trustworthy, one firmware update

### A1. Correct actuator handling and preserve a checkpoint

- Use one shared effective voltage-limit calculation in velocity PI and PWM
  application, with measured bus voltage. Make conditional integration or
  back-calculation follow actual applied voltage, including endpoint and duty
  limits. Reset or transfer integrator state explicitly at test boundaries.
- Log requested, unclamped and applied voltage, PI components and saturation.
  Test the case RUN_VOLTS=6 V, bus=24 V: anti-windup must recognize the 3.6-V
  actuator ceiling. Test reversal recovery and bus-voltage changes.
- Keep voltage/duty ceilings fixed during gain identification. A higher voltage
  test changes the plant operating envelope and needs a separate experiment.
- Keep S2, driver faults, existing current/voltage/thermal supervision, encoder
  continuity and single-turn limits. Classify their failures accurately. New
  analysis quality checks should mark data inconclusive, not become arbitrary
  normal-motion fault gates.

### A2. Runtime configuration and exact readback

Add RAM-only, atomic configuration staging/readback for velocity Kp/Ki,
position Kp, profile RPM and acceleration. Read firmware build ID, selected
axis/mode, calibration/zero identity, effective limits and a configuration hash.
Every trial records the acknowledged active values, not a header on the laptop.
Avoid flash writes during candidate search. Export the accepted profile once.

Changes are applied at firmware-defined trial boundaries with a stationary
reference and explicit controller-state handling. Do not abruptly switch gains
and a wound-up integrator while the motor is moving.

### A3. MCU-timestamped traces and local trial execution

Capture at the existing 1-kHz supervisor rate initially:

- MCU microsecond timestamp, sample sequence, trial/configuration ID;
- actual encoder sample timestamp/raw angle, unwrapped position, velocity and
  filter state; encoder status and age;
- requested target, trajectory position/velocity, position error, velocity
  setpoint/error;
- P term, integral term, requested/applied Vq, saturation reason and duty;
- bus voltage, phase-current diagnostics with their validity, peak current and
  temperature with timestamps, state/fault and first-fault snapshot.

Use binary batches and a bounded ring buffer; allocate from the linker map
rather than assuming unlimited H743 RAM. For example, an initial 2048-record,
64-byte ring uses 128 KiB; confirm actual packing/fields/bank availability first.
Batch transmission (e.g. 8 samples per UDP payload) keeps packet overhead and
formatting out of the control ISR. Validate CPU/ADC timing under streaming load.
Current acquisition stays at its existing rate; a decimated current sample is
not silently labelled RMS current or identified torque.

The MCU runs the excitation/reference sequence from its clock. Ethernet starts,
monitors and retrieves it; Windows sleep/packet jitter must not time the actual
voltage or velocity pulses. Detect dropped samples, distinguish transport delay
from physical delay, and reject deficient identification records.

## Phase B: characterize this loaded mechanism before optimizing

1. Noise and zero repeatability: record stationary angle at several positions;
   measure RMS/peak-to-peak noise, drift, timestamp gaps and velocity noise.
   Repeat calibration and compare electrical offset and response. Inspect the
   AS5600 filter/read latency. User's weak-field allowance stays visible; a
   +/-1-degree encoder reading is not proof of +/-1-degree physical accuracy.
2. Polarity and actuation: confirm positive/negative Vq produces the expected
   current/initial motion in both directions at several shaft angles. Large
   angle-dependent asymmetry is a reason to check commutation/magnet/load,
   not to compensate blindly with higher gains.
3. Breakaway and load: short, locally timed, bipolar voltage perturbations or
   controlled slow traversals identify the voltage needed to initiate/maintain
   motion in either direction at several poses. Separate friction/cogging from
   gravity-dependent holding effort where the data permit. All testing stays
   in an interior window, initially around a usable central pose.
4. Dynamic excitation: start with bounded short bipolar pulses; progress to
   small multisine/PRBS bursts only after polarity/motion/noise are understood.
   Select amplitude above the measured noise/deadband without clipping or
   violating the available travel. A test need not span 0..360 degrees.

Use a stable existing loop for closed-loop identification only after proving
it remains stable at the selected pose. The previous oscillatory loop must not
be assumed a suitable closed-loop-ID baseline. Alternatively use short local
open-velocity/voltage experiments under the independent travel/current monitor.
Do not use sustained relay oscillation to find an ultimate gain at the endpoints.

If identifying in closed loop, use the independent injected excitation and a
closed-loop-consistent frequency-domain/identification method. Naive least
squares on feedback-correlated actuator input and noisy velocity can be biased.
Record the excitation separately from total applied voltage.

## Phase C: derive and validate velocity gains

Fit the lowest-order model that describes the operating region. One candidate:

    d(omega)/dt = a*u_applied - b*omega - c*sign(omega) - d(position)

omega is rad/s, u_applied is V. This identifies effective voltage-to-motion
parameters; it does NOT separately identify physical inertia, torque constant
and resistance without additional measurements. Include transport/electrical
delay or an extra pole if needed. Fit directions/poses separately if necessary.
Avoid raw differentiation of quantized angle; prefer state-space/output-error
fitting or suitable filtering with delay accounted for.

For the linear part G(s)=a/(s+b) and PI C(s)=Kp+Ki/s, ignoring delay only for
this analytic starting estimate, the characteristic equation is:

    s^2 + (b + a*Kp)*s + a*Ki = 0

Matching to desired damping zeta and natural frequency wn gives:

    Kp = (2*zeta*wn - b)/a
    Ki = wn^2/a

These are calculated candidate gains in this firmware's units, not numbers
to use before measuring a,b and delay. Reject invalid/negative candidates;
then include measured delay/filtering, saturation and nonlinear load in discrete
simulation/frequency-response validation. Require model confidence and holdout
prediction before increasing bandwidth. Do not transplant an amp-output servo's
inertia-based gains directly into our volt-output controller.

Tune velocity first with position correction isolated in an explicit tuning
mode. Test both directions, short durations and bounded excursions. Confirm
disturbance rejection without persistent oscillation. Friction/gravity
feedforward is considered only after it is measured; if used, include it inside
the real applied-output anti-windup accounting. Derivative is not a default
answer to a noisy speed estimate.

## Phase D: position tuning, then trajectory optimization

Freeze the accepted velocity gains. Choose a slower outer position bandwidth
using the measured inner-loop response; a fivefold separation is an initial
design choice to verify, not a proven constant for this mechanism.

For an ideal much-faster velocity loop, position P gives local error dynamics
de/dt=-Kpos*e. The corresponding settling estimate is
Kpos=ln(initial_error/tolerance)/desired_time. This supplies a starting value;
validate the actual cascaded/discrete model, trajectory feedforward and limits.
Optimize 10/45/90-degree moves both directions at multiple interior poses.

Only after position behavior is repeatable, increase trajectory acceleration
and speed separately with bracketed trials. Fit a feasible acceleration/braking
envelope rather than using a single fastest no-load run. If abrupt reversal
excites the mechanism, evaluate an S-curve/jerk limit as a profile improvement,
not as a substitute for stable loops.

For a rest-to-rest triangular move of D degrees with acceleration A RPM/s:

    peak_RPM = sqrt(A*D/6)

Thus A=75 and D=356 only reaches about 66.7 RPM before deceleration, even with
a 100-RPM ceiling. Use the measured stopping distance and actual torque/voltage
envelope to choose limits; raising a ceiling that is never reached saves no time.

## Python commissioning tool and automatic experiment workflow

Proposed tools (not implemented by this planning task): a Python CLI/dashboard
with readback, trace plots, experiment orchestration, model fitting, candidate
simulation, bounded validation and rollback. Use Python numeric/control tools;
MATLAB is not required to implement the cited principles.

Workflow:

1. Read firmware configuration/identity; save an immutable baseline artifact.
2. User completes mechanical setup, calibration/zero and verifies the interior
   test window/load. One physical S1 press authorizes a specified bounded batch.
3. Run measurement/identification experiments locally on the MCU. Retrieve
   trace batches, assess quality, fit model and validate on held-out records.
4. Derive candidate velocity gains; screen candidates in simulation before
   hardware trials. Apply candidates at trial boundaries with readback.
5. Automatically run a small ranked set of candidates, compare repeated results
   and keep/restore the best validated profile. A poor model means more
   measurement, not a wider random gain search.
6. Freeze inner gains; tune outer position and then trajectory in separate
   phases. Revisit the model if changing load/voltage/temperature changes it.
7. Output model parameters with uncertainty, Bode/step/trace plots, active
   firmware configuration, all rejected-trial reasons and a recommended profile.
   Persist the profile to source only after review; no automatic gain guessing
   or unattended repeated flash/recalibration cycle.

Automatic retries of a normal test segment must not reset a fault or auto-rearm
after S2/disconnect. Keep the existing stop/driver/encoder/travel protections;
limits are agreed at batch setup and enforced by the MCU, not by laptop timing.
No new position-sweep heuristics become motor shutdown conditions. Local batch
state distinguishes completion, operator stop, travel/zero, driver, feedback,
communication, instability and test-data-quality failures.

## Objective, scoring and acceptance

Optimize time-to-enter-and-remain within +/-1 degree. Initial stability-scoring
window: 0.5 seconds, with <=0.5-degree peak-to-peak position variation; confirm
noise permits that before adopting it. These are experiment scoring criteria,
not additional firmware trips. Log overshoot, final error, RMS tracking error,
reversals, peak/continuous current evidence, voltage saturation duty, sample
quality and thermal trend. Do not improve the score just by loosening tolerance.

Use repeated trials and a worst-case/upper-percentile settling objective, not
the fastest individual move. At least three repetitions both directions at
several operating angles precede accepting a candidate; final validation uses
previously unseen moves/load poses. Keep speed/acceleration tuning separate
from gains so a slower trajectory cannot masquerade as a better controller.

Starting measurements and short repetitions can be automated; motor thermal
qualification cannot be inferred from the PCB NTC alone. A motor-case probe or
other appropriate independent measurement is needed for an extended envelope.
Absolute physical +/-1-degree accuracy also needs an independent position
reference if the weak-magnet encoder's accuracy has not been established.

## Implementation order and stop points

1. Instrumentation + consistent anti-windup + runtime config/readback; test on
   host and all existing build profiles before hardware. No gain retuning yet.
2. Firmware trace timing/transport qualification; repeat the baseline and explain
   saturation/noise/nonlinearity from actual synchronized data.
3. Identification and model-derived velocity candidates; validate locally.
4. Position/trajectory tuning and automated batches with saved rollback.

The first deliverable should be phase A and a measurement viewer, not another
list of hardcoded suggested Kp/Ki values. Network motion automation is possible
after that interface exists and a STATUS query succeeds; it is not available
merely because the current position-command client can reach the board.

## Primary sources

1. MathWorks, Design Multiloop Control System:
   https://www.mathworks.com/help/control/ug/design-multiloop-control-system.html
2. SimpleFOC, Cascade Position Control (voltage-mode cascade and loop order):
   https://docs.simplefoc.com/angle_cascade_control
3. MathWorks, How PID Autotuning Works (excitation, frequency response, tuning):
   https://www.mathworks.com/help/slcontrol/ug/how-pid-autotuning-works.html
4. MathWorks, Anti-Windup Control Using PID Controller Block:
   https://www.mathworks.com/help/simulink/slref/anti-windup-control-using-a-pid-controller.html
5. Synapticon, System Identification (mechanical restrictions, friction/noise):
   https://doc.synapticon.com/node/oblac/tuning/auto-tuning/system_id.html
6. MathWorks, Simulate and Predict Identified Model Output (holdout validation):
   https://www.mathworks.com/help/ident/ug/definition-simulation-and-prediction.html
7. MathWorks, Closed-Loop PID Autotuner motor example (existing stable loop is
   a prerequisite; FOC example is methodology, not a request to change our mode):
   https://www.mathworks.com/help/slcontrol/ug/tune-field-oriented-controllers-using-closed-loop-pid-autotuner-block.html
