# Circuit Tools and Validation

## Recommended formats (practical stack)

1. `drawio` for fast communication and concept flow.
2. `circuitikz` (`.tex`) for publication-quality static schematics.
3. `SPICE` (`.cir`) for behavior checks before soldering.
4. KiCad for production schematics/PCB (best long-term source of truth).

## Files in this folder

1. `01-Q1-Failsafe-12V-Pullup.drawio`
2. `failsafe-driver-circuitikz.tex`
3. `failsafe-driver-ltspice.cir`

## Quick start

1. Open the `.drawio` in draw.io for architecture-level edits.
2. Open the `.cir` in LTspice and run `.op` with stepped `MODE`.
3. Verify:
   - `MODE=0` (GPIO low): load should be ON.
   - `MODE=1` (GPIO high): load should be OFF.
   - `MODE=2` (GPIO hi-z / ESP off): load should be OFF (failsafe).

## Editing options

1. LTspice (Windows app): easiest for this netlist.
2. ngspice (CLI): can run the same netlist with minor syntax tweaks.
3. LaTeX Workshop (VS Code): edit/build `circuitikz` diagrams (`.tex`).
4. Draw.io Integration (VS Code) or draw.io desktop/web: edit `.drawio`.

## VS Code extensions and setup

1. `LaTeX Workshop` (publisher: James Yu) for `.tex` schematic preview/build.
2. Install a TeX distribution:
   - Windows: MiKTeX or TeX Live
3. Open `failsafe-driver-circuitikz.tex` and run build from LaTeX Workshop.
4. Output is a PDF schematic.

## Your current error and fix

If you see:

1. `spawn latexmk ENOENT`
2. `kpsewhich returned with non-zero code 1` for `standalone.cls`

Then TeX tools/packages are missing on your machine.

Fix sequence:

1. Install MiKTeX (or TeX Live).
2. Make sure `latexmk` and `pdflatex` are in PATH.
3. In MiKTeX Console, install packages:
   - `standalone`
   - `circuitikz`
4. Restart VS Code/Antigravity.
5. Open `failsafe-driver-circuitikz.tex` (not the extension output log tab).
6. Run recipe:
   - `pdflatex x2 (fallback)` first
   - then `latexmk (default)` once PATH is confirmed.

If you see:

1. `MiKTeX could not find the script engine 'perl'` for `latexmk`

Use `pdflatex`-only workflow (no Perl required):

1. Workspace is already configured for this in `.vscode/settings.json`.
2. Build with recipe `pdflatex x2 (default, no perl)`.
3. Do not select `latexmk` recipe unless you install Perl.

## Is it LaTeX?

1. Yes. `failsafe-driver-circuitikz.tex` is LaTeX using the `circuitikz` package.
2. You edit the text source; the rendered output is a PDF.

## Bench validation checklist

1. Measure Q2 gate when ESP is held in reset: gate should be near 0V.
2. Confirm no direct 3.3V pull-up remains at Q1 base node.
3. Confirm all grounds are common (12V supply, ESP, INA260, load return).
4. Check GPIO current in failsafe condition stays below 1 mA.
