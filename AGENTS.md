# Agent instructions

These rules apply to every AI agent working in this repository.

## Language

- **Chat replies** to the user are written in **Indonesian** (Bahasa Indonesia).
- **Everything written into the repository** is written in **English**: code, identifiers, code comments, log and error messages, commit messages, pull request titles and bodies, and all documentation (`README.md`, `docs/`, and any other `.md` or `.txt` file).
- Quoted game output, file contents, and user-supplied data keep their original language.

## Documentation layout

- `README.md` is the landing page: what the project is, supported versions, quick start, and an index of the other docs. Keep it short.
- Detailed docs live under `docs/`:
  - `docs/setup.md`: prerequisites, setup, generate, build, troubleshooting.
  - `docs/running.md`: launcher usage, options, look knobs, controls.
  - `docs/status/current.md`: the present state, verified results on the latest build, open gaps, next validation steps.
  - `docs/status/history.md`: dated results and past measurements.
  - `docs/e2e/`: e2e harness guide, trace variables, experiment switches.
  - `docs/analysis/`: research and design notes.
  - `docs/sources-and-licensing.md`: source inventory, third-party notices, review rules.
- When verification results change, update `docs/status/current.md` and move the superseded result into `docs/status/history.md`. Do not leave two conflicting "current" statements.
- Do not link to `PLAN.txt`. It is a local planning file and is not part of the repository.
- Write dates in full (`16 October 2026`). Write decimals with a point (`0.6`) and thousands with a comma (`50,233`).
- Keep docs factual and in the present tense unless they describe history.

## Repository rules

- `.gitignore` denies everything by default. A new source or doc file is ignored until it is whitelisted in `.gitignore`. Source files also need a review entry in `outputs/FM4-source-control/tracked-files.json`.
- Run `outputs/FM4-source-control/check-staged-content.py` before committing.
- Do not commit game data, executables, DLLs, `BuildRoot` contents, captures, saves, or logs.
- Do not commit or push unless the user asks for it.
