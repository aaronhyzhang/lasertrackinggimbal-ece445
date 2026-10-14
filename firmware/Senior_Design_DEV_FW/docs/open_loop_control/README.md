# Bidirectional open-loop control guide

This guide documents firmware v0.5.0, source commit e0acf5c, for the H753 physical RUN-controlled bench profile. It contains no later motor-feedback architecture.

## Compile

Keep the folder structure intact and run from this directory:

    pdflatex -interaction=nonstopmode -halt-on-error open_loop_control.tex
    pdflatex -interaction=nonstopmode -halt-on-error open_loop_control.tex

Or use:

    latexmk -pdf open_loop_control.tex

Overleaf: upload the complete folder/ZIP (including figures), set open_loop_control.tex as the main document, and select pdfLaTeX. All packages are common TeX Live/MiKTeX packages. No shell escape, TikZ, external bibliography processor, or Python execution is needed to compile the supplied source. Figures are already supplied as PDFs.

## Separate figures

Each file in figures/ has a vector PDF for LaTeX and an editable SVG for independent use:

- 01_system_flow: command/power flow and supervisory inhibition.
- 02_state_machine: startup, continuous neutral qualification, wake/alignment/run/coast/fault.
- 03_pwm_and_phase_voltages: one half bridge, complementary logic and three phase envelopes.
- 04_analog_speed_map: signed analog-speed mapping and center band.
- 05_reversal_timing: generated speed and drive enable during a full-scale reversal.

The diagrams are illustrations derived from the code, not measured waveforms. Deadtime gaps are exaggerated and reversal times are approximate. All PDF/SVG figures are standalone; the LaTeX contains only includegraphics references.

To edit the vector source programmatically, generate_figures.py uses Python 3 and reportlab:

    python -m pip install reportlab
    python generate_figures.py

Python and reportlab are optional diagram-authoring tools, not firmware or LaTeX build dependencies. source_counts.json records the exact file list and counting method used in the document. Gross source inventory is not the number of lines executed in open loop.

The main operating checklist remains ../OPEN_LOOP_BOARD_CONTROL.md. No firmware settings or runtime code were changed to create this explanation.

## Verification

The supplied source compiled successfully with Tectonic 0.17.0 to a 12-page Letter document. Final LaTeX logs contain no overfull/underfull boxes or undefined references. The compiled pages and all five vector figures were visually reviewed. The main compiled PDF and QA page images are local verification intermediates; the deliverable is the LaTeX source and separate figure assets.
