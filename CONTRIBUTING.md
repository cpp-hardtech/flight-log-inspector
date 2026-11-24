# Contributing

## Before you start

Open an issue for changes that affect the log format, public API, or CLI behavior. Include a
small reproducible example when reporting a parsing issue.

## Workflow

1. Create a topic branch from the default branch.
2. Keep each pull request focused on one logical change.
3. Build the project, run CTest with `FLIGHT_LOG_ENABLE_UNIT_TESTS=ON`, and document the inputs
   you used to verify the change.
4. Match the surrounding C++ style. Add tests for new behavior in `tests/`.

Prefer generated or minimized test logs when adding fixtures. Include the source and license
information for any third-party material in [NOTICE](NOTICE).

Contributions are distributed under the terms in [LICENSE](LICENSE).
