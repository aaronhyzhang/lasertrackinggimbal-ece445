# Measured model regression

`voltage_06v_50ms.csv` contains only the model input columns from the completed
2026-10-27T15:56:29Z axis-0 capture: 0.6 V alternating excitation, 50 ms period,
1.2 s, 1 kHz records. Voltage is commanded q-axis phase amplitude; velocity is
the controller's encoder estimate in rad/s. Times are uint32 MCU microseconds.

This reproduces premature optimizer termination at the original 80-evaluation
budget and the empty positive-Kp candidate grid that ended at 24 rad/s. It is a
local identification regression, not proof of physical model accuracy or
closed-loop performance. User logs retain the complete trace and metadata.
