# Logged position tuning

No firmware reflash is required for this script update. Start with Ethernet
source, calibrated mechanical zero and SERVO_READY. From the CONTROL folder:

```powershell
py -3 .\Tools\position_sweep.py --axis 0 --tune
```

Press S1 once when prompted. The sequence is 90, 100, 90, 135, 90, 270, 90
degrees: an initial positioning move, then small and large movements both ways.
S2/Ctrl-C stops. Every completed move must satisfy the same measured dwell as
the ordinary sweep. The default full-range/random sweep also records reports:

```powershell
py -3 .\Tools\position_sweep.py --axis 0
```

Each invocation writes logs/position_<UTC timestamp>.csv and .json, including
partial results after a fault/timeout. CSV contains laptop elapsed time, target,
measured position/RPM, state/fault, current/encoder ages and firmware settled.
JSON contains per-move duration excluding pre-arm wait, first arrival within
2.5 degrees, peak observed RPM, directional overshoot, final error, sampling
gaps and extra settling beyond the required 0.5-second dwell. A sampled 90th
percentile acceleration estimate is included; this is noisy and not a hardware
acceleration limit. The terminal prints a summary and one proposed parameter
change. Successful identical plans compare total measured time against the
previous successful report in the same log directory. Negative percent is
faster; accept an improvement only if overshoot and repeatability remain good.

Configuration labels come from local Core/Inc/control_config.h, NOT a firmware
readback. Confirm the file matches the image flashed for that run; --config can
select a different header. No script writes gains or changes firmware settings.
Keep the same load, supply, axis, origin and test plan when comparing runs.

The recommendation order is deliberately conservative:

- Following fault: lower acceleration 20%; do not widen the following limit.
- Other incomplete run: diagnose the actual fault/timeout first.
- A settling timeout with fresh, fault-free RUNNING observations is analyzed
  for gain advice too. Repeated crossings outside +/-2.5 degrees suggest a
  diagnostic integral-off trial (CONTROL_VELOCITY_KI_V=0), not more speed.
- With integral already zero, a final two-second window with <=1 degree travel,
  <=2 RPM and error >2.5 degrees suggests a 30% velocity proportional-gain trial.
  This indicates a stationary offset, not a proven friction or commutation cause.
- Observed overshoot >2.5 degrees: trial a 25% integral-gain reduction.
- Long final correction without large observed overshoot: trial 10% more
  position gain, watching for new hunting/overshoot.
- Otherwise: trial 10% more RPM ceiling if already near it; else 10% more
  acceleration. Change just the suggested parameter, rebuild/reflash, recalibrate
  and repeat. Restore the previous values if the result gets worse.

These are experiment suggestions, not a proven tuning diagnosis or automatic
optimizer. At ~12 Hz the samples can miss peak speed, oscillation or overshoot;
there is no trajectory/voltage readback to distinguish torque saturation from
poor loop tuning. No safe thermal or absolute performance maximum is inferred.
Repeated passing sweeps plus current and motor-case-temperature observation
are required before calling a setting usable for longer runs.

For review, share the generated JSON and CSV rather than a terminal screenshot.

Reanalyze an existing pair without moving the motor:

```powershell
py -3 .\Tools\position_tuning.py .\logs\position_<timestamp>.json
```

This preserves the original log and writes a separate _analysis.json file.
