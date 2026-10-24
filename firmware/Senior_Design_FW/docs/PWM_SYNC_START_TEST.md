# Synchronized PWM startup comparison

2026-10-24. This is a controlled firmware experiment, not a confirmed fix for J4's phase-B high-side VDS fault. The user confirmed J3 reached MOTOR_OPEN_RUNNING with axis=0/stage=3 and healthy driver status on the preceding image. J4 still captured R0=0x608/R1=0 after replacing its B-high MOSFET and driver IC.

## What changed

`PCB_PWM_SYNC_START=1` makes each first PWM authorization stop the selected timer with MOE off, temporarily leave center-aligned mode to select up-counting, write all three duties, force an update with UDIS clear, reset the count to zero and restore center-aligned mode while stopped. Existing live-fault/permission checks remain before output release. MOE is released and the counter starts in the same IRQ-masked section. Only UPDATE status is cleared by preparation; the existing authorization code retains its separate break/EXTI handling.

Regular running duty writes do not reset the count or force an update. A STOP/coast and subsequent restart prepares the timer again. The flag set to 0 restores the previous counter-phase behavior for an A/B comparison. Shared driver registers, fault thresholds, gate current, dead time, motor limits, analog mapping, buttons and fault latch are unchanged.

At a near-50% initial duty, count zero/up gives a known first high interval of roughly 12.5 us before the compare; this differs from a complete steady-state center-aligned high interval of roughly 25 us. This experiment removes an arbitrary initial counter phase; it does not promise every first interval is a complete steady-state pulse. Gate-driver protection may still truncate it.

## Bench procedure

Keep both motors disconnected throughout this comparison. In the existing Senior_Design_PCB_BRINGUP project, Core/Inc/pcb_config.h is prepared as:

```c
#define PCB_STAGE PCB_MOTOR_RUN
#define PCB_TEST_AXIS 0
#define PCB_PWM_SYNC_START 1
#define PCB_BENCH_ALLOW_CPUV_HISTORY 0
#define PCB_BENCH_CAPTURE_DRIVER_FAULT 1
```

1. Refresh the project in CubeIDE if necessary, clean/build and flash the existing project's Debug ELF, then Resume.
2. Test J3 first: release buttons, center VR1, press/release S1, then move VR1 slightly outside neutral. Confirm stage=3, axis=0 and MOTOR_OPEN_RUNNING without fault. This checks the startup change against the working bridge; do not attach a motor to this capture-enabled diagnostic build.
3. If J3 passes, power off and change only PCB_TEST_AXIS to 1. Rebuild/flash/Resume. The same buttons and VR1 command J4.
4. Test J4 the same way. If it faults, record g_driver_fault.state/read_mask/reg[0]/reg[1] and the first-pulse waveform. CH1=J4 ENABLE TP34, CH2=B high input TP25, CH3=B low input TP26, CH4=verified shared nFAULT/PE6. All probe grounds go to board GND. Use an ENABLE-referenced recording to avoid confusing the initial wake fault pulse with the final trip; zoom to 2-5 us/div near the final nFAULT falling edge.
5. If J3 fails, stop the J4 comparison and report its fault registers. If J4 still records 0x608, the controlled counter start did not resolve that protection event; do not infer a software or hardware root cause solely from this result. Do not disable the protection to continue switching.

The active project remains axis=0 for the first test. The source repository defaults remain IO_CHECK/axis=0/capture=0, with the startup feature available by default. Active Core before the change is backed up under tmp/pcb_bringup/before_sync_start/Core in Senior_Design_FW. No agent flash or physical switching test was performed.

## Verification and limits

Host checks cover both axes' motor/app/driver routes and invalid configurations; actual-BSP tests cover capture off/on with STOP/fault inhibition. The vendor timer-init audit exercises both axes x PWM_CHECK/MOTOR_RUN with sync off/on, including an arbitrary initial count/down direction, first preparation, non-rephasing running updates, and restart. It runs on private Windows RAM with simulated CPU/NVIC and status-clearing semantics. It does not emulate active/shadow register transfer, hardware timer counting, clock timing or gate voltages. Physical first-pulse timing remains to be measured.

The change follows the installed STM32H7 HAL/CMSIS timer definitions: CEN, CMS/DIR, CCR preload, UG and write-zero-to-clear status semantics. DIR is selected while stopped in edge-aligned mode, rather than written while center-aligned. Firmware builds are required for both timer selections before bench use.
