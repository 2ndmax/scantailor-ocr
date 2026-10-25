# ScanTailor OCR — working principles

These rules apply to every change in this repository. Building is described in
[BUILDING.md](BUILDING.md), the tests in [TESTING.md](TESTING.md).

## Code principles

1. **Core code reports errors, it does not show them.** Code outside the GUI
   (filters, image I/O, PDF/OCR, pipeline, StageSequence) must not open dialogs
   (QMessageBox etc.) or read widget/GUI state. Report errors to the caller via
   return value or exception; the GUI displays them, a future command-line
   interface prints them.
2. **No new GUI dependencies in core code.** The core must stay usable without
   a GUI, because a command-line interface is planned (StageSequence::processAll
   is the first step towards it).
3. **Fix once, not per copy.** If a bug sits in duplicated code, merge the
   duplicates into one shared function first, then fix it there.
4. **Readability over brevity.** Guards get a descriptive name or a short
   comment explaining *why*. Do not stack workarounds on top of each other.
5. **Refactoring does not change behavior.** Keep refactoring and behavior
   changes in separate commits.

## Review notes in code

When a review finding is deliberately not fixed, the spot is marked with a comment:

```cpp
// REVIEW-NOTE (YYYY-MM): <condition under which the problem occurs> -> <consequence>.
// Not fixed because <reason>.
```

Example: `// REVIEW-NOTE (2026-10): images wider than 65535 px make 'scale' zero
-> division by zero. Not fixed because input width is limited by ImageLoader.`

Future reviews treat a spot with a REVIEW-NOTE as settled as long as the code
below it is unchanged. If a one-line guard costs no more than the comment,
add the guard instead.

## Tests

- A bug fix comes with a regression test where possible. The test must fail
  before the fix; if it does not, the bug is not confirmed and nothing is changed.
- Tests must not depend on the machine: no user settings, no files outside the
  test's own temporary directory.

## Formatting

CI checks every changed .cpp/.h under `src/` with clang-format 22.1.3 and the
repository's `.clang-format`. Run the same version on changed files before
committing, e.g. `clang-format --dry-run -Werror <files>`, and fix with `-i`.

## Texts

The UI source language is American English; German is the only translation and
must stay complete. After changing visible texts, run
`cmake --build build --target update_translations` and translate new entries.
