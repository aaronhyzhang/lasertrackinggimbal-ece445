# J4 VDS threshold comparison

**Historical diagnostic procedure:** active-image selections below are superseded by [the post-repair baseline](POST_REPAIR_OUTPUT_CHECK.md); temporary experiment flags are now off.

2026-10-24. The user requested increasing OCP threshold after the 45 mA source-current image still read R0=0x608/R1=0. That image verified register 3=800, so its selected source-current profile reached the driver. This next experiment changes only the MOSFET VDS threshold; it is not a confirmed fix.

## Meaning and settings

The previous profile used VDS_LVL=000: 0.15 V across an enabled MOSFET. The new step uses VDS_LVL=001: 0.24 V. TI Table 18 documents both encodings. Register 5 changes 0x300 -> 0x301, decimal 768 -> 769. Its other fields remain 400 ns dead time, latched OCP and all-bridge shutdown. No retry/report-only/disabled mode is selected. The fixed VDS deglitch time remains 4.5 us.

This is not a bus-voltage limit, shunt-voltage threshold or regulated motor-current target. For a fully enhanced MOSFET, VDS is approximately current times RDS(on), but resistance depends on gate voltage and temperature; a MOSFET failing to turn on can exceed the threshold without high winding current. The separate shunt-sense OCP threshold remains 0.25 V, approximately 17.9 A across a 14-milliohm shunt. This firmware still does not implement a calibrated 4 A current loop.

Only 150/240 mV are exposed in this comparison; unsupported values fail compilation. The backend writes and verifies the selected register value. Other profile entries, high-side source=45 mA, sink currents, TDRIVE, neutral guard, synchronized startup, STOP and fault latching are unchanged from the preceding attempt. Firmware current settings are not measurements of physical gate voltage.

## Active project and bench test

Core/Inc/pcb_config.h in the existing Senior_Design_PCB_BRINGUP project:

```c
#define PCB_STAGE PCB_MOTOR_RUN
#define PCB_TEST_AXIS 1
#define PCB_PWM_SYNC_START 1
#define PCB_HS_SOURCE_MA 45
#define PCB_VDS_THRESHOLD_MV 240
#define PCB_BENCH_ALLOW_CPUV_HISTORY 0
#define PCB_BENCH_CAPTURE_DRIVER_FAULT 1
```

Keep both motors disconnected and use the existing current-limited bench supply. Clean/build/flash Debug, Resume, center VR1, press/release S1 and move slightly outside neutral. Check g_driver_status.verified=true, reg[3]=800 and reg[5]=769. If these do not match, verify the flashed image before drawing conclusions. Other expected configuration values are reg[2]=128, reg[4]=768, reg[6]=640.

If it still faults, record g_driver_fault.state, read_mask, reg[0] and reg[1]. If it remains healthy, observe the phase outputs with grounded probes referenced only to board GND and report the result; keep the motor disconnected because capture=1 is a diagnostic shutdown policy. A passing test suggests sensitivity to the selected threshold; it does not prove the physical design or switching current is acceptable. A phase that remains far below the drain rail while commanded on is expected to exceed either threshold.

Revert only PCB_VDS_THRESHOLD_MV to 150 to restore the prior threshold, then rebuild. Pre-change Core backup: Senior_Design_FW/tmp/pcb_bringup/before_vds240/Core. Repository/source archive defaults remain IO_CHECK, axis=0, source=15 mA, VDS=150 mV, capture=0. No agent hardware flash or new physical result is claimed.

## Verification

Host coverage runs 30 executables and 12 compile guards. Driver variants exercise both axes, both source-current choices, both VDS thresholds and both CPUV-history policies. Exact readback is required; mismatches, transport failures and reported faults are still rejected. Tests check that the selected threshold does not change latched/all-bridge shutdown, dead time or CSA protection. Active J4 Debug and Release must pass native CubeIDE builds without warnings. These checks do not establish analog protection thresholds, transient currents or switching waveforms on hardware.

Primary source: TI SLVSE39B, Table 18, electrical characteristics and section 7.3.5.3: https://www.ti.com/lit/ds/symlink/drv8304.pdf
