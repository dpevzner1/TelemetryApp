# TelemetryApp UI Layout Harness

This harness is mandatory for every new TelemetryApp UI, installer, dialog, wizard, sidebar, HUD, and context-menu change.

## Hard Rule

No visible text may be clipped, compressed, overlapped, hidden behind scrollbars, hidden behind installer footers, or forced into unreadable controls. A feature is not complete until the UI layout passes this gate.

## DevOps Engine Basis

The read-only DevOpsAgent contract requires UI/UX expectations, accessibility, desktop compatibility, DPI/scaling, interaction states, and native performance acceptance criteria to be treated as engineering requirements before implementation.

## Required Checks

- Use explicit vertical bands for title, input controls, helper text, validation/status text, and action buttons.
- Do not place helper text on the same row as dense input controls unless measured width is clearly sufficient.
- Give every button horizontal padding and enough width for its longest label.
- Keep installer controls above the NSIS footer line; do not use the bottom strip for feature text.
- Prefer short labels plus separate helper text over long single-line labels inside tight controls.
- For every new scrollable area, verify wheel, drag, track click, and thumb click behavior.
- For every modal dialog, verify OK, Cancel, close button, Tab order, and Enter/Escape behavior.
- For network actions, avoid blocking UI long enough for Windows to mark the app Not Responding.
- Validate at normal Windows scaling and assume 125%/150% DPI can occur.

## Acceptance Gate

Before packaging:

1. Build native binaries successfully.
2. Inspect changed UI surfaces for clipped or compressed text.
3. Verify text does not overlap controls or status regions.
4. Verify scrollbars and buttons remain clickable.
5. Update README or project docs when workflow text changes.
6. Rebuild installer only after the native build passes.

Any failure is a remediation blocker, not cosmetic backlog.
