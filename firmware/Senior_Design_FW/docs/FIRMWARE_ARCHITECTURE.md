# Firmware architecture

Latest DEV behavior: see the v0.5.0 signed-speed / neutral-start section at the end; earlier revision notes are historical.

Updated: 2026-10-12 (America/Chicago). Active implementation: [Senior_Design_DEV_FW](../../Senior_Design_DEV_FW), STM32H753ZIT6. Software builds and host tests pass; hardware validation is pending. [Firmware guide](../../Senior_Design_DEV_FW/docs/FIRMWARE_GUIDE.md), [board settings](../../Senior_Design_DEV_FW/docs/BOARD_CONFIG.md), and [validation record](../../Senior_Design_DEV_FW/docs/VALIDATION.md) describe the actual code. [Vendor research](ARCHITECTURE_RESEARCH.md) explains the architectural choices.

## Selectable open-loop addition — 2026-10-12

Firmware v0.2.0 adds an independent `open_loop.c` path for analog commanded speed, default maximum 30 RPM, with 10 RPM/s reference ramp, 1 V timed alignment and 2 V rotating-vector magnitude. Encoder, zero, position/velocity loops and one-turn travel checks are bypassed only in OPEN_LOOP. Common PWM, driver break/wake, ADC/liveness/timing/stop/watchdog supervision remains. Actual RPM/stall/current are not regulated or verified in this mode. See [open-loop guide](../../Senior_Design_DEV_FW/docs/OPEN_LOOP_GUIDE.md).

`MODE OPEN_LOOP|CLOSED_LOOP` is an inhibited-only foreground request applied by the control IRQ. It resets calibration/reference/controller/command authority and selects ANALOG; a pending mode request cannot race ARM and is canceled by stop. Logical armed OPEN_IDLE refuses mode/source changes even with bridge coasting. Open-loop ARM requires a fresh near-zero ADC; raising analog then performs wake/alignment/run. Zero ramps frequency to zero, then coasts; DISABLE/! inhibit immediately. Switching back requires new closed-loop calibration and ZERO.

The `--profile open-loop` build selects only the boot default via `DEMO_DEFAULT_OPEN_LOOP=1` and emits a separate image under `build/OpenLoop/`. Both modes share one source/BSP and remain available at runtime. Open-loop analog input is separately typed in volts and never reinterprets a position command as speed. Serial/Ethernet motion setters reject open-loop mode; network STATUS/DISABLE remain available.

## Scope and ownership

The initial demonstration uses NUCLEO-H753ZI, BOOSTXL-DRV8304H, one confirmed DFRobot FIT1036/AS5600, a 24 V bus, and ST-LINK virtual COM. Analog 0–3 V -> bounded 0–360° position is the first acceptance milestone; Ethernet follows through the same servo. Camera tracking, CAN, two-axis operation and current-regulated FOC remain future work. Final custom PCB is H743ZIT6 with DRV8304S, LAN8742 and low-side 14 mΩ shunts; its board port needs independent validation.

Control/protocol code lives once in DEV_FW. H743 later links portable modules with its own board adapter. This change leaves the H743 application/configuration intact. Canonical project context stays here; operational code-specific guides live alongside DEV firmware.

The demo uses hand-owned HAL initialization. Its inherited IOC represented custom-board pins/clocks and is archived under DEV_FW/docs/archive. CubeMX project nature was removed so historical generation cannot silently replace the Nucleo BSP. A future regenerated project must match the implemented board record and be compared in a temporary directory first. Vendor HAL/CMSIS files remain separate from application code.

## Implemented modules

| File in DEV_FW/Core | Responsibility |
| --- | --- |
| Src/main.c | Thin boot sequence and cooperative foreground loop |
| Inc/demo_config.h | Named configuration values with physical units and conservative defaults |
| Src/board.c | H753 clock/pin/timer/ADC/I2C/UART/watchdog bindings, coherent input publication, immediate inhibit and break latch |
| Src/motor_control.c | Portable axis state, calibration, encoder unwrap/velocity, trajectory, position P, velocity PI, anti-windup and sine modulation |
| Src/demo_app.c | One-axis integration, 1 kHz ownership, selected-source latest target, one pending action, control/foreground liveness and status snapshots |
| Src/demo_console.c | Bounded VCP input/output, command parsing; immediate UART ISR stop on ! or RX loss |
| Src/demo_network.c | LwIP raw UDP, PHY/link, bounded foreground RX, persistent DMA buffers and session ownership |
| Src/demo_protocol.c | Portable strict ASCII/integer parser and unsigned half-range sequence comparison |
| Src/stm32h7xx_it.c | Exceptions inhibit drive; IRQ routing; control timer service |

Tools/build.py builds H753 Debug/Release images; Tools/test.py runs portable C and Python client tests; Tools/udp_client.py supplies interactive Ethernet degree targets and heartbeat. Middlewares/LwIP is pinned to CubeH7's LwIP 2.1.2 with its license. No empty CAN/current-control scaffolds were added.

The initial axis implementation combines several tightly related responsibilities in motor_control.c. HAL and transport dependencies are excluded from it. Split these responsibilities into reusable modules when integrating a second axis/current loop, rather than duplicating globals or the BSP.

## Control path

Selected-source target -> bounded unwrapped trajectory -> position P -> velocity PI -> q voltage clamp -> inverse Park / three-phase sine modulation -> TIM1 preloaded duty commit.

AS5600 raw angle -> single-writer unwrap / calibrated direction / mechanical zero / filtered velocity -> motion feedback and electrical commutation angle. Electrical angle is modulo 2π; mechanical travel/error remains unwrapped so 0° and 360° represent different endpoints of a full-turn move.

This is voltage-mode encoder-oriented sinusoidal control, not measured-current FOC. Position gain has units (rad/s)/rad; velocity PI returns volts. Current sensing, bus measurement and phase-voltage diagnostics are absent in this dev profile. Configured 24 V and ±2 V effort are not measured bus/current protections. Bench current limiting and verified EVM hardware protection are required.

Defaults: 11 pole pairs, 1 V alignment, ±2 V q effort, 0.4 rad/s speed, 0.4 rad/s² acceleration, 50 ms velocity filter. Position/velocity/trajectory all run at 1 kHz in this first implementation; decimation is not assumed to imply bandwidth. Update filtered velocity only when an actual new encoder sample arrives. Conditional integration prevents winding up against the q-voltage clamp. At 2 V on a configured 24 V bus, sine duties are within the modulation clamp, so no additional modulation saturation is expected at this profile. Validate actual bus and scaling before changing that assumption.

A 30° following-error fault catches large trajectory/feedback disagreement. Feedback outside −5°..365° trips travel protection. These are software fault margins, not mechanical stops. Reference speed/acceleration limits are tested; physical limits and closed-loop tracking need bench evidence. Nominal full-turn reference duration is about 17 s plus settling.

## Scheduling, concurrency and memory

| Work | Context / setting |
| --- | --- |
| PWM | TIM1 hardware, center aligned 20 kHz nominal, complementary outputs, 500 ns nominal deadtime |
| Axis/supervisor/trajectory/P/PI | TIM7 IRQ, 1 kHz nominal; reject measured periods outside 700–1500 µs or execution >500 µs |
| Encoder / ADC | Nonblocking launches from control tick; bounded callbacks publish coherent raw samples |
| Magnet health | Separate I2C read every 20 ms; does not overwrite raw angle timestamp |
| Commands / network / formatting | Cooperative foreground, bounded serial bytes and ≤4 RX frames per network call |
| Watchdog | Foreground refresh only while the control timebase is recent; nominal ~1 s IWDG |

Interrupt priorities: break 0, UART 1, control 2, sensor 3, SysTick 4, Ethernet 5. UART is above control solely to support immediate ! inhibition. Short critical sections protect sample tuples, actions, stop latch and status copies. The control IRQ is the sole writer of motor/controller state and normal PWM duty. Foreground target updates replace stale pending targets rather than queuing a history. One pending action is accepted; busy actions are rejected. Stop cancels pending actions and latches a board interlock before later actions can reauthorize it.

No blocking waits, printf, dynamic allocation or network stack run in control IRQ. Boot sensor configuration is blocking only while inhibited. One I2C transaction is outstanding; no catch-up bursts. Encoder acquisition may skip a tick for a health read. Data older than 8 ms, magnet status older than 60 ms, ADC older than 20 ms, or active foreground older than 100 ms inhibits drive. There is no automatic active-drive I2C recovery; failed encoder configuration requires repair/reboot.

HSI-derived CPU 200 MHz, AHB 100 MHz, APBs 50 MHz, timer kernels 100 MHz avoid inherited 25 MHz HSE assumptions. I-cache is on; D-cache stays off. DMA descriptors and persistent payload pools occupy explicit aligned .eth_dma in D2 SRAM at 0x30000000; no payload points into DTCM. TX slots are released only on descriptor completion, never reused just because a send timed out. Linker reserves 8 KiB stack; stack watermark and execution timing still need bench measurements. Retest memory coherence if enabling D-cache/MPU regions later.

## Startup, calibration and faults

DISABLED -> CAL_WAKE -> CAL_ALIGN -> CAL_SWEEP -> CAL_RETURN -> READY -> ARM_WAKE -> RUNNING. Any excitation state can fault; DISABLE coasts into an inhibited state. Fault clear returns to DISABLED with calibration and mechanical zero invalidated.

Calibration requires healthy encoder data before enabling the driver. Wake keeps PWM inhibited. Alignment ramps d voltage, then a +90° electrical sweep verifies direction and motion magnitude; returning to alignment verifies return and records electrical offset. Success disables the bridge and leaves ZERO/ARM as explicit subsequent actions. ZERO records the current shaft orientation as mechanical 0° while inhibited. It is not automatic homing or persistent absolute multi-turn state.

TI transiently asserts nFAULT during wake/sleep. Hardware BKIN2 remains active throughout, with MOE clear during wake. Break IRQ notification is enabled only once drive is authorized and fault input is high. Expected wake assertion therefore cannot create an interrupt storm or repeat wake. An active-drive break clears MOE in hardware and latches software inhibition. Automatic output restart is disabled.

Exception handlers, explicit stop, encoder/ADC/driver/command/liveness/timing/tracking/travel faults all inhibit output. Later duty calls recheck the latched board interlocks. CLEAR_FAULT cannot energize a motor; CALIBRATE, ZERO and ARM must be repeated. External ENABLE pull-down is required for reset/unpowered MCU. Arbitrary active debug halt and loaded gravitational holding are not validated; program/debug with bus disconnected. Demo disable is immediate coast.

## Command ownership and Ethernet

Analog uses PA3/ADC1, measured VDDA scaling, a 3 V command full scale and endpoint clamp. A reading above 3.15 V is invalid. A disconnected target wire can appear as valid 0 V; ADC freshness is not a cable-disconnect detector. Separate arm is mandatory.

VCP is USART3 PD8/PD9, 115200 8N1. STATUS, CALIBRATE, ZERO, ARM, DISABLE, CLEAR_FAULT, SOURCE, POSITION and HEARTBEAT are documented in the firmware guide. Actions return QUEUED and must be checked via STATUS. Serial and Ethernet motion require valid POSITION/HEARTBEAT within 500 ms; analog does not depend on a network heartbeat. Source changes are allowed only while inhibited and invalidate previous target authority.

LwIP NO_SYS=1 uses foreground raw UDP and sys_check_timeouts; no socket/netconn API or RTOS is required. Static IP 192.168.10.2/24, UDP 5005. Strict bounded ASCII G1 packets carry nonzero session, uint32 sequence, operation and integer millidegrees. This supersedes the initial fixed-binary packet proposal because human-readable captures help the deadline/demo. There is one axis; no axis-mask field is falsely advertised.

Only selected owner IP/port/session POSITION/HEARTBEAT packets refresh command authority. Duplicate/older sequence values, malformed packets and STATUS/ping do not refresh it. New owners acquire while inhibited after old authority expires. DISABLE remains an independent stop from any bench peer. Network cannot ARM/CALIBRATE/ZERO; VCP supplies those deliberate actions. Authentication remains a deployment consideration, not a claim of the bench protocol. Local control checks expiry even if foreground stalls.

## Later current FOC and H743 transfer

Add calibrated timer-triggered current samples only after verifying EVM shunt/gain/scales and low-side conduction windows. Replace voltage effort with d/q current PI, constrained by measured bus/modulation/sample windows and applied-effort anti-windup. Current ADC completion becomes the sole fast duty writer; remove the old competing 1 kHz voltage duty writer. Tune current, then velocity, then position.

H743 port requires custom clock/PHY/SPI bindings, DRV8304S configuration/readback, confirmed 14 mΩ shunts/gain, current offsets/polarity, phase/bus dividers and independent axis instances. Both encoders have address 0x36 and need separate I2C buses as drawn. Shared ADC scheduling and two-axis shutdown remain open: TIM1 BKIN2 does not automatically disable TIM8. See HARDWARE_REFERENCE.md and ARCHITECTURE_RESEARCH.md. Existing custom reset ENABLE pulls and nominal-24 V capacitor margin need assembled-board review.

Final camera/laptop/gimbal semantics remain undecided; camera is on the gimbal and the entire object must stay in central 80% ROI. Preserve that context without encoding an unchosen image-vector protocol into this motor demo.

## Evidence and remaining work

Debug/Release compile, native CubeIDE integration and host tests are recorded in DEV_FW/docs/VALIDATION.md. No firmware has been flashed, no EVM waveforms/current thresholds measured and no physical performance established. Next acceptance sequence remains: logic/sensors -> PWM/shutdown -> calibration -> analog servo -> Ethernet -> rehearsal. Numeric gains/limits freeze only after bench results. Vendor-backed architecture and tested failure paths are engineering evidence, not MISRA/functional-safety certification.

## One-line configuration and staged switching checks — 2026-10-13

Firmware v0.2.1 normal/native builds use the single `DEMO_DEFAULT_OPEN_LOOP` header line: 1=open-loop speed, 0=closed-loop position. Current default is 1; no automatic arm is added. Explicit CLI profile overrides are optional and stored separately. MCU PWM validation precedes connecting the gate driver; powered driver gate/source/non-overlap/shutdown checks precede connecting the motor. [Bench switching guide](../../Senior_Design_DEV_FW/docs/BENCH_SWITCHING_VALIDATION.md) explains the shared 20 kHz complementary carrier, nominal 500 ns deadtime, 120° duty envelopes, fixed-angle alignment, gradual electrical rotation and probe references. Safe complementary switching alone does not prove rotor synchronization; record loaded waveforms/current/RPM and hardware assumptions.

## MCU PWM scope test — 2026-10-13

The current H753 v0.3.0 header selects DEMO_PWM_SCOPE_TEST=1 for a bench without comms. It bypasses the motor state machine: PA3 above 100 mV directly sets identical 5–95% duty on all three complementary pairs at nominal 20 kHz/500 ns deadtime. PG4/driver ENABLE remains LOW; PE6 high permits and low stops pulses, with automatic diagnostic resume. ADC invalid/stale/overrange inhibits. Encoder, console and Ethernet are not started. This verifies MCU waveforms only, not motor commutation. Set the test switch to 0 and rebuild before gate-driver/motor stages; normal motor authorization and alignment remain. The detailed procedure is in the DEV project docs/PWM_SCOPE_TEST.md. Hardware measurements are pending.

## Physical open-loop bench control — 2026-10-13

The user reports successful MCU PWM/complementary switching observation. Current H753 v0.4.0 defaults select motor drive (scope=0), open loop and PG2 physical RUN control. PG2 CN11 pin 42 is active high with pull-down; LOW stops on the next nominal 1 ms tick. ARM needs 20 ms LOW then 20 ms HIGH with PA3 <=100 mV, valid/fresh ADC and a disabled controller. Boot/reset with RUN already high never arms; failed nonzero-pot edges are consumed. PE6 remains actual nFAULT with hardware break; fault release/RUN toggling never clears the latched fault, and RESET plus a fresh RUN transition is required. Yellow PE1 LD2 indicates armed; red PB14 LD3 indicates fault. No console/network/encoder transactions start in the physical profile. Existing wake/alignment/rotating sinusoidal field/acceleration and voltage limits remain. Pot zero ramps down/coasts, RUN low revokes authorization. Full harness and power-stage gate checks are in DEV docs/OPEN_LOOP_BOARD_CONTROL.md. Closed-loop and optional serial-open-loop behavior remain available before flashing. Gate/motor/current/shutdown timing bench evidence is pending.

## Phase-B Nucleo GPIO correction — 2026-10-13

H753 DEV v0.4.1 changes only the B-low physical output: PE10/TIM1_CH2N, CN12 pin 47 (also D40/CN10 pin 24), replaces PB0/CN11 pin 34. PB0 is shared with default green LD1; removing this load also matches the standard Nucleo CH2N header. Prior firmware left PE10 unconfigured, so a prior D40 connection could have picked up neighboring PWM instead of carrying a driven complement. The user reports raw B waveform distortion with the driver disconnected, while A/C look cleaner; exact prior physical header/inversion remains unconfirmed. TIM1 timing/commutation/RUN/fault logic are unchanged. Power off, ensure INLB is on PE10, rebuild/flash, and repeat MCU-only captures before EVM/motor testing. Physical resolution is pending. See DEV docs/PHASE_B_PWM_DIAGNOSTIC.md.

## Open-loop speed-range step — 2026-10-13

The user reports the motor spins using the original speed range. H753 DEV v0.4.2 raises DEMO_OPEN_MAX_RPM from 30 to 100 over the same 0–3 V input, with 100 mV zero/ARM deadband unchanged. New sensitivity is 33.333 RPM/V (3.333x prior). Fixed 2 V run effort, 1 V/2 s alignment and 10 RPM/s acceleration/deceleration are unchanged; full-scale ramp takes approximately 10 s after alignment and full-scale zero-command deceleration may take 10 s. RUN-low remains sampled immediate inhibit. At 100 RPM/11 pairs, field frequency is 18.333 Hz. Higher-speed synchronism, actual RPM, current/temperature and loaded switching remain pending; low fixed voltage can limit attainable speed. Firmware does not increase voltage automatically or regulate current. Current maps and software checks are in DEV docs/OPEN_LOOP_BOARD_CONTROL.md and build_evidence.json.

## H753 v0.5.0 signed analog speed and neutral-start protection — 2026-10-13

This supersedes the preceding one-way / 100 mV motor-start instructions. The user reports motor rotation and that the previous 100 RPM range looks good. Current DEV defaults remain physical PG2 RUN, no console/network/encoder transactions. PA3 is now 0 V=-100 RPM, 1.3–1.7 V inclusive=zero, 3 V=+100 RPM; equal 1.3 V active spans give 76.923 RPM/V (2.308x the previous sensitivity). A fresh RUN LOW->HIGH request made outside center waits with PWM/ENABLE off until valid/fresh center input remains continuous for 200 ms. Leaving center or losing ADC/foreground freshness resets dwell. Yellow LD2 blinks at 2 Hz while waiting, steady when armed; red LD3 means latched fault. RUN LOW/stop/fault cancels waiting. Boot/reset with RUN HIGH still never arms until LOW is observed and a fresh HIGH request occurs. Fault recovery remains reset plus a fresh request/center dwell.

Core/Src/demo_app.c owns pending RUN/neutral dwell; Core/Src/open_loop.c owns signed field integration, alignment and reversal. Reversal ramps field speed to zero at 10 RPM/s, inhibits gates/ENABLE for 1 s, then repeats 10 ms driver wake and 2 s alignment before ramping in the new direction. Center also ramps to zero/coasts without energized holding. The pause is provisional and not measured rotor standstill. Alignment is still 1 V (0.5 s ramp, 2 s total), run vector 2 V, configured bus 24 V, 11 pole pairs. Current regulation, measured RPM/stall detection and closed-loop gimbal validation remain later work. Closed-loop analog position mapping remains 0–3 V -> 0–360 degrees; MCU scope-test mapping remains unipolar direct duty with 100 mV off.

0 V now commands reverse: remove any old PA3 ground pull-down used as an unplugged-input stop. If required, equal 10 kOhm resistors to 3.3 V/GND bias an unplugged lead near 1.65 V; verify pot loading and actual endpoints. Firmware cannot detect a disconnected analog signal held low once armed. PG2 LOW remains the independent drive inhibit. Physical startup/reversal/RPM/current/temperature and loaded gate timing for v0.5.0 must be checked. Current procedures and software evidence are in DEV docs/OPEN_LOOP_BOARD_CONTROL.md, OPEN_LOOP_GUIDE.md, VALIDATION.md and build_evidence.json.
