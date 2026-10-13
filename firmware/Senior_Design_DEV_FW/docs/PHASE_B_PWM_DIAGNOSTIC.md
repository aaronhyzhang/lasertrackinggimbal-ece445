# Phase-B PWM waveform investigation

Updated 2026-10-13, firmware v0.4.1. Inputs remain physical RUN PG2 and analog speed PA3; no control algorithm or timer timing changes.

## Observation and software review

The user supplied IMG_2094.jpeg and IMG_2095.jpeg showing raw MCU PWM for phase B with edge spikes/stepped levels and reported A/C looked cleaner. The captures show approximately 20.1 kHz and channel-1 peak-to-peak readings of 4.44–4.56 V. The user subsequently confirmed the driver was disconnected and the probes/settings matched the other phases. Full channel scales, zero markers, physical header/probe assignments, inversion and coupling are not shown, so these photos cannot establish actual logic plateaus, deadtime or complementary polarity. Peak-to-peak includes edge excursions and is not necessarily the logic-high voltage.

TIM1 CH1/CH2/CH3 use the same PWM1 mode, center-aligned carrier, polarity and complementary-channel configuration. Deadtime and break are global. CCR1/2/3 preloads are written together and latch at a timer update. The sinusoidal field gives different slowly changing compare values by phase, but should not create analog shoulders, spikes or another voltage level within a pulse. No other code writes PB0 as an LED GPIO; software LED writes are PB14 (fault) and PE1 (armed).

PB0 was a valid AF1 TIM1_CH2N allocation, but it is connected to Nucleo green LD1 through SB39 in the default assembly. This is a real phase-B-only load; it has not been proven to explain the observed waveform. Another plausible cause is physical header mismatch: the standard Nucleo D40/TIM1_CH2N header is PE10, while prior firmware drove PB0. Probing or wiring D40 with the prior build would use an unconfigured input that could pick up neighboring PWM. The user identified timer signal names, but the exact physical header is still being clarified. PE10 is an alternate AF1 TIM1_CH2N output with a header connection and no LD1 load. The implementation now uses PE10; PB0 is an input with pull-down. This removes that board load without modifying commutation, carrier frequency, deadtime or the RUN/fault logic.

## Required rewiring

With motor/driver power off, move the single INLB wire:

| Signal | Prior v0.4.0 | Current v0.4.1 | Driver destination |
| --- | --- | --- | --- |
| B high | PE11, CN12 pin 56 | PE11, CN12 pin 56 | J4 pin 5 / INHB |
| B low | PB0, CN11 pin 34 | **PE10, CN12 pin 47** (also D40, CN10 pin 24) | J4 pin 7 / INLB |

Refresh/build/flash in CubeIDE and verify the wire before powering the driver. PB0 will no longer output PWM. If INLB/probing was already at D40/CN10 pin 24 or CN12 pin 47, it was already on PE10; no additional move is needed after flashing this update. Verify physical header names rather than assuming a timer name uniquely identifies a pin. Do not simply move the wire without flashing the updated firmware: old firmware does not configure PE10. Do not energize the driver with the old INLB connection after flashing this revision.

## Separate measurement from circuit behavior

1. Physically unplug the entire EVM signal harness, not just its bus supply; an unpowered input can still load a signal. Keep motor power disconnected.
2. For a fixed-duty isolation check, temporarily select DEMO_PWM_SCOPE_TEST=1, rebuild/flash, pull PE6 high through a resistor and set PA3 to approximately 1.5 V. PG4 stays LOW. All three pairs should have identical ~50% high-side duty, 20 kHz carrier and nominal 500 ns both-low gap. Observe PE11/PE10 directly at the Nucleo headers before reconnecting any harness. Scope mode uses PE10 too.
3. Use two compensated 10x probes, DC coupling, matching attenuation settings, channel inversion OFF and short local ground springs/leads to Nucleo GND. Disable the leftover gate-voltage math trace for clarity. Swap probes/channels between the pins: a distortion that follows a probe suggests a measurement problem; one that stays at the pin suggests the board/output/harness.
4. Measure high/low plateaus separately from overshoot. At 10 us/div confirm the square carrier; zoom around 100–200 ns/div for paired transitions. Both outputs should not be HIGH together, with a both-LOW gap. If both appear to rise together, verify channel inversion, exact header/pin assignment and whether the weak trace is crosstalk from a floating/unconnected lead.
5. Scope the same output with and without a long jumper. Shortening probe grounds/wiring can reduce apparent ringing. Bandwidth limiting may help diagnose high-frequency pickup but must not hide switching problems during final non-overlap checks.
6. When the isolated PE11/PE10 pair is clean, restore DEMO_PWM_SCOPE_TEST=0 and reflash. Reconnect the verified 6-PWM EVM harness with windings still disconnected. Check logic at INHB/INLB, real nFAULT and gate-to-source switching. If distortion appears only after connecting the driver, investigate harness grounding, wrong header, unpowered/back-powered inputs and actual input loading; it is not automatically a commutation-code issue.
7. Confirm all phases, RUN/fault shutdown, actual VGS non-overlap/ringing and current consumption before motor attachment. Encoder-free open loop still cannot detect actual rotor synchronization.

No new physical measurements of PE10 or proof of a root cause were available when this change was made. The user confirmed the driver was unplugged; exact physical B-low header, channel mapping and inversion remain to be clarified.

References: [ST UM2407 LED solder bridges and headers](https://www.st.com/resource/en/user_manual/um2407-stm32h7-nucleo144-boards-mb1364-stmicroelectronics.pdf), [STM32H753 datasheet Table 9 alternate functions](https://www.st.com/resource/en/datasheet/stm32h753zi.pdf), [Tektronix probe grounding/ringing guidance](https://www.tek.com/en/documents/application-note/probing-techniques-accurate-voltage-measurements-power-converters-oscillos).
