# Working pan checkpoint, 2026-10-27

J4/axis1 pan, representative mechanism; 24 V. Sinusoidal voltage control;
current PI disabled. Velocity Kp .43, Ki42.3; position Kp5; speed110RPM;
normal accel199.502RPM/s, <=20deg near-rest targets capped114.066RPM/s;
run/ceiling6V. Calibration2V succeeded; CW-stop zero, positiveCCW;
physical estimated339.8deg, firmware commands2..337, tested sweep15..320.
Tilt retains independent .215/18.795/5.5,110RPM,150RPM/s,6V defaults.

Small/medium/large 1kHz captures passed: 10deg worstarrival.291s,
overshoot.645deg;50deg .406s/.86deg;150deg .718s/.079deg.
Position-based completion requires finished trajectory, +/-1deg, <=.5deg
span for250ms. Final mixed12move sweep passed withoutfault, finalerrors
<=.381deg, sampledovershoot<=.445deg. Several completion times improved,
not uniformly. Script retains0.5s dwell and +/-2.5deg pass;~12Hz sampled
sweep metrics do not prove continuous +/-1deg or thermal capability.

NativeDebug build0errors/warnings;hostcontrollerregressions and49Python
tests passed. Earlier validation_evidence.json predates this checkpoint
and must not be interpreted as hashes for these files. No dualaxis
operation qualified by this checkpoint. Rawlogs remain local.
