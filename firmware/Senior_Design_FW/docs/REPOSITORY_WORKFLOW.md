# Repository setup

Updated 2026-10-13.

- Remote: https://github.com/aaronhyzhang/lasertrackinggimbal-ece445
- Dev branch: `dev-firmware-h753`, tracking `origin/dev-firmware-h753`.
- Base: `firmware-initial-upload`, which contains the H743 project. At setup time `main` contained only the initial README.
- Initial dev commit: `e394401` — Add H753 dev-board motor firmware and bring-up documentation.
- Review: https://github.com/aaronhyzhang/lasertrackinggimbal-ece445/compare/firmware-initial-upload...dev-firmware-h753

The Git checkout is at `Senior_Design_FW/tmp/github_publish/lasertrackinggimbal-ece445` in the original CubeIDE workspace. Its project paths are `firmware/Senior_Design_DEV_FW` and `firmware/Senior_Design_FW`. The uploaded source copy passed host tests and both ARM builds; all recorded source and BIN hashes matched the validated development snapshot.

The existing CubeIDE DEV project is a separate source copy. Future changes made there need to be copied into the Git checkout before committing, or edit/import the project from the Git checkout directly. CubeIDE cannot import two simultaneously open projects with the same project name. The H753 project uses hand-owned initialization and the historical IOC is archived.

From the Git checkout, normal future updates are:

```powershell
git status
git diff
git add <reviewed-source-files>
git commit -m "Describe the change"
git push
```

Commit source, configuration, tests and documentation. Build outputs, logs, caches and local downloaded PDFs are ignored or excluded. Preserve vendor licenses. Run `Tools/test.py` and `Tools/build.py` from the H753 project as documented before pushing firmware changes.

## Historical open-loop update — 2026-10-13

Commit `d92426c` on the same dev branch adds `MODE OPEN_LOOP|CLOSED_LOOP`, an analog one-way speed path, and the `--profile open-loop` default-open image. The tested source is also synchronized into the original active DEV project. Its images are at `build/OpenLoop/Debug` and `build/OpenLoop/Release`; at that revision, normal closed-loop-default images were at `build/Debug` and `build/Release`. Both modes are runtime-selectable only while inhibited. See the active DEV project's `docs/OPEN_LOOP_GUIDE.md` for commands, provisional 30 RPM limit, hardware assumptions and bench gates.

## Current one-line mode selection — 2026-10-13

Pushed commit `c9a26570e2cf8bc664b5bf965ee4f588a57434ad` on `dev-firmware-h753`. Change `DEMO_DEFAULT_OPEN_LOOP` in `Core/Inc/demo_config.h`: 1 is open-loop analog speed, 0 is closed-loop analog position. Normal CubeIDE and CLI builds now use this header line (currently 1); rebuild and flash normally. Explicit CLI overrides remain under `build/OpenLoop` and `build/ClosedLoop`. Runtime mode commands remain available while disabled; no mode change automatically arms the motor.

The original active DEV project is synchronized and independently rebuilt. Its 31 source hashes and six Debug/Release BIN hashes match the repository evidence. Host tests pass; native CubeIDE Debug/Release have zero errors/warnings. Nothing was flashed or measured on hardware. See `docs/BENCH_SWITCHING_VALIDATION.md` in the DEV project for MCU-alone, disconnected-motor EVM and unloaded-motor validation, gate probing and startup timing. Remote HEAD was checked against the local commit, and the Git checkout is clean.

## Pot minimum adjustment — 2026-10-13

Firmware v0.2.2 sets DEMO_OPEN_ZERO_VOLTS and DEMO_OPEN_ARM_MAX_VOLTS to 0.10 V, accommodating the reported approximately 40 mV potentiometer minimum. Active DEV source is synchronized. Configured Debug/Release builds and retained host/client tests pass; native CubeIDE must rebuild before flashing. Commit 530c0cf is on the same dev branch.

## Automatic PWM scope test — 2026-10-13

Commit 8186c92 (v0.3.0) adds DEMO_PWM_SCOPE_TEST=1 as the current header-selected mode. It starts identical complementary ADC-controlled PWM without communications/ARM and holds PG4 ENABLE LOW. PE6 high permits, low stops, release resumes. This is MCU-only waveform validation; no motor commutation. Set the switch to 0 and rebuild to restore motor firmware. Active DEV sources match and native CubeIDE Debug/Release builds pass without errors/warnings; six CLI scope/open/closed builds and host tests pass. Full procedure: DEV docs/PWM_SCOPE_TEST.md. Bench evidence is pending.

## Physical RUN open-loop control — 2026-10-13

Firmware v0.4.0 selects DEMO_PWM_SCOPE_TEST=0, DEMO_DEFAULT_OPEN_LOOP=1, DEMO_OPEN_BOARD_CONTROL=1. PG2 CN11 pin 42 RUN requires stable low then high at <=100 mV pot; low stops, boot high never arms. No console/encoder/network transactions run in this profile; PE6 is real driver nFAULT. Active DEV native Debug/Release and all host tests pass. Gate/motor validation is pending after user-reported complementary MCU PWM. Same dev branch contains the update; see DEV docs/OPEN_LOOP_BOARD_CONTROL.md.

## Phase-B pin correction — 2026-10-13

Pushed v0.4.1 commit 979c4ce on dev-firmware-h753. B-low TIM1_CH2N moves from PB0/CN11 pin 34 to PE10/CN12 pin 47 (D40/CN10 pin 24); B-high remains PE11. Removes LD1 load and matches standard CH2N header. GPIO-only change, no PWM timer/commutation/RUN algorithm change. All six profile builds and active native Debug/Release pass; source/docs are synchronized. Actual cause/physical waveform fix is still pending; user confirmed driver disconnected during distorted raw-MCU captures. See docs/PHASE_B_PWM_DIAGNOSTIC.md.

## 100 RPM analog range — 2026-10-13

H753 DEV v0.4.2 raises max open-loop command from 30 to 100 RPM across 0–3 V, preserving 100 mV deadband/ARM, physical RUN, 10 RPM/s ramp and 2 V run effort. The user reports baseline motor rotation; higher speed remains to validate physically. All retained tests, six profile builds and active native Debug/Release builds pass. Source/docs match the pushed dev branch. See OPEN_LOOP_BOARD_CONTROL.md for updated voltage-to-RPM points and 10 s ramp/coast timing.

## Bidirectional analog and wait-for-neutral — 2026-10-13

Pushed v0.5.0 commit e0acf5c962c5c35e43c126dc870b25a3903abaed to dev-firmware-h753; remote HEAD verified and checkout clean. Active Senior_Design_DEV_FW source/docs synchronized, all 35 recorded source hashes match. Mapping is 0 V=-100 RPM, 1.3–1.7 V inclusive=neutral, 3 V=+100 RPM. Physical RUN request outside center waits with gates off until continuous valid center input for 200 ms; yellow LD2 blinks waiting, steady armed. RUN LOW/stop/fault cancels waiting; boot-high RUN still needs stable LOW then HIGH. Reversal ramps field speed to zero, disables/coasts 1 s, repeats wake/2 s alignment then ramps opposite. The pause is not measured standstill. Equal 10 kOhm resistors to 3.3 V/GND can bias unplugged PA3 neutral; old PA3 ground pull-down would command reverse. All host/controller/app/RUN/scope/default/client tests and six ARM builds pass; active native CubeIDE Debug/Release rebuilt with zero errors/warnings. User reports prior spin/100 RPM range looks good, but new physical bidirectional startup/reversal and quantitative RPM/current/temperature remain pending. No agent flash. Updated procedures and evidence are in DEV docs/OPEN_LOOP_BOARD_CONTROL.md, OPEN_LOOP_GUIDE.md, VALIDATION.md and build_evidence.json.

## Bidirectional open-loop LaTeX explanation — 2026-10-13

Added DEV docs/open_loop_control/open_loop_control.tex with five standalone PDF/SVG vector figures, build/reuse instructions and exact source counts for firmware v0.5.0 / e0acf5c. Covers only physical RUN open-loop dependencies, motor magnetic principles, signed-speed mapping, modulation/PWM, timing, neutral startup and reversal, source responsibilities and software/bench limits. Source compiles with Tectonic 0.17.0 to 12 pages; rendered document/figures reviewed, no TeX layout/undefined-reference warnings. User asked for LaTeX and separate block diagrams; compiled main PDF remains a QA intermediate. Final source package is output/open_loop_control_source.zip. No runtime/configuration changes.

Published documentation commit ee0ee5af944eaa02811046e40f6dc68751c2cce1 on dev-firmware-h753; remote HEAD verified and checkout clean. Active DEV docs synchronized. ZIP verified byte-for-byte against final LaTeX and all five PDF/SVG figure pairs.

## Standalone presentation project — 2026-10-14

Created sibling Senior_Design_OPEN_LOOP_DEMO and published commit cbb215294ecc5739bf33dd9086e2a77c2aed90c4 on dev-firmware-h753; local/remote HEAD match and checkout is clean. The project has only the physical open-loop application plus selected ST HAL/CMSIS support, unique CubeIDE metadata, no source links to DEV, no feedback/console/network/scope paths. Original 35 development source/test/tool hashes remain unchanged. Core application/BSP/runtime and headers total 691 physical lines, excluding vendor system/startup/Drivers. Focused host tests pass; 84,462 synthetic actual-app ticks match the original physical profile with zero numeric difference, including states/faults/authorization/LEDs. Critical board functions and active board_apply body retained verbatim. CLI Debug/Release and independent native CubeIDE builds pass without warnings. Source archive output/Senior_Design_OPEN_LOOP_DEMO_source.zip is verified (76 files). README/source map and build evidence are in the new project. No flash/physical validation of the extracted copy performed; low-speed bench recheck needed. Copies are independent; future fixes require explicit port/re-extraction.

## Custom-PCB open-loop bring-up - 2026-10-23

Created independent active Senior_Design_PCB_BRINGUP and repository firmware/Senior_Design_PCB_BRINGUP, for STM32H743ZIT6. User confirms schematic/DRV8304S/SiS890ADN; J3 first, VR1, center-stop bidirectional, S1 ARM/S2 STOP, gain 20 V/V, minimum 15/30 mA gate current, STLINK-V3MINI, 24 V bus / proposed 2 A supply limit. Schematic tracing resolves VR1 as POT1/PA4/ADC1_INP18/TP12 (not PA0); J3 B-low is PB0. New target normalizes full onboard ADC travel, nominal neutral 1.43..1.87 V; endpoints -100/+100 RPM. J4 remains GPIO low / ENABLE low. Foreground SPI explicitly configures/checks the S driver after each wake; nFAULT is TIM1 hardware break. STOP invalidates in-flight configuration permission. Default one-line stage selection is IO_CHECK, then PWM_CHECK, DRIVER_CHECK, MOTOR_RUN. Original H753 projects are unmodified.

Actual controller/app/driver host tests pass; ten CLI ARM builds (all stages Debug/Release plus configured-default IO) pass without warnings. Independent active native CubeIDE Debug/Release import/build pass with zero errors/warnings; active source hashes match repository evidence. README and docs/BRINGUP.md give the pin/test-point audit, STLINK-V3MINI-to-J6 wiring, stage procedures and measured-results checklist; docs/VALIDATION.md and build_evidence.json separate software checks from pending hardware measurements. Canonical PCB_BRINGUP/HARDWARE_REFERENCE updated. No agent flash or custom-board motor run. ENABLE pull-up/reset behavior, real current/temperature and C44 full-bus transient margin remain bench checks. Published on pcb-bringup-h743; build outputs excluded from Git.
