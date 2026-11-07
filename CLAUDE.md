# ScanTailor OCR — working principles

These rules apply to every change in this repository. Building is described in
[BUILDING.md](BUILDING.md), the tests in [TESTING.md](TESTING.md).

## Code principles

1. **Core code reports errors, it does not show them.** Code outside the GUI
   (filters, image I/O, PDF/OCR, pipeline, StageSequence) must not open dialogs
   (QMessageBox etc.) or read widget/GUI state. Report errors to the caller via
   return value or exception; the GUI displays them, the command line tool
   `scantailor-ocr-cli` prints them.
2. **No new GUI dependencies in core code.** The core must stay usable without
   a GUI: the command line tool runs it through `StageSequence::processAll`
   (`CommandLineRun`), and `core_tests` tests it under `QCoreApplication`.
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

Future reviews treat a spot with a REVIEW-NOTE or an explaining comment as
settled as long as the code below it is unchanged. If a one-line guard costs
no more than the comment, add the guard instead.

## Working through a review

1. Check every finding against the current code before acting on it; the
   review's severity and its claims are not taken over unchecked.
2. Sort each finding into one of these groups and handle it accordingly:
   - Settled: the spot has a REVIEW-NOTE or explaining comment and the code
     below it is unchanged -> nothing to do.
   - False alarm: the claimed problem cannot happen -> a one-line comment at
     the spot saying why, if the code invites the same misreading again.
   - Not reachable today: the problem would need inputs or callers that don't
     exist -> REVIEW-NOTE (or the one-line guard, see above).
   - Suspected bug: a claim about what some input does -> regression test
     first (see Tests); no failing test, no change.
   - Wrong on reading alone -> fix (see the exception under Tests).
   - Changes what users see, or a design question -> decided by the maintainer
     before anything is built.
3. Commits: one per fix, refactorings apart from the fix, all comments and
   REVIEW-NOTEs of a review in one closing `docs:` commit.
4. Hand tests of all fixes of a review are done once, together, at the end.

## Tests

- A bug fix comes with a regression test where possible. The test must fail
  before the fix; if it does not, the suspected bug is not confirmed and
  nothing is changed.
- Exception: code that is wrong on reading alone (it contradicts its own
  purpose, comment or name, or has undefined behavior) is fixed even when no
  test can reach it. A test is still added where one is possible. Without one,
  the spot is marked with
  `// Fixed without a test: <what was wrong>; <why no test reaches it>.`
  If the fix changes behavior a user can see, it needs a hand test, bundled
  with the other hand tests of the same work. A missing guard for a case that
  cannot occur is not wrong code; it gets a REVIEW-NOTE instead.
- Any other fix without a regression test gets a REVIEW-NOTE at the fixed spot
  saying why no test is possible.
- A bug found by fuzzing also adds its input to the seeds in `src/fuzz/corpus`.
- Tests must not depend on the machine: no user settings, no files outside the
  test's own temporary directory, no image files in the repository (draw the
  pages in the test).

More on running and writing tests: [TESTING.md](TESTING.md).

## Documentation

When a change matters for one of the Markdown files, ask the user whether to
add it there; do not update them on your own. Ask only for changes that matter
to the reader of that file, above all for CHANGES.md:

- CHANGES.md: user-visible changes (features, fixed bugs a user could notice,
  changed behavior or file formats).
- BUILDING.md, TESTING.md, `scripts/README.md`, `scripts/analysis/README.md`:
  new or changed build options, test programs, developer tools and scripts.

Details are written in one place only; the other files link to it.

## Commits

Commit messages are in English. The first line starts with a prefix (`feat:`,
`fix:`, `refactor:`, `perf:`, `test:`, `docs:`, or the area such as
`analysis:`, `setup:`, `website:`) and says what is true afterwards, e.g.
`fix: the command line tool refuses an empty --export-folder`. The body explains why.

## Formatting

CI checks every changed .cpp/.h under `src/` with clang-format 22.1.3 and the
repository's `.clang-format`. Run the same version on changed files before
committing, e.g. `clang-format --dry-run -Werror <files>`, and fix with `-i`.

## Texts

The UI source language is American English; German is the only translation and
must stay complete. After changing visible texts, run
`cmake --build build --target update_translations` and translate new entries.
