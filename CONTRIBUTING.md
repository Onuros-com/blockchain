# Contributing to Onuros

Thank you for helping improve Onuros. The project is building practical private
money and treats consensus safety, mandatory privacy, deterministic behavior and
reproducible performance evidence as core requirements.

## Workflow

1. Open an issue before substantial consensus, cryptography or economic changes.
2. Create a focused branch from the appropriate current branch.
3. Keep consensus rules explicit and fail closed.
4. Add tests for successful behavior, rejection paths and boundary conditions.
5. Run the complete local test suite.
6. Open a pull request; do not push directly to `main`.

For Stage 5 maintenance, use `local-blockchain`. Current Stage 6 private
transaction work belongs on `private-transactions`.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Linux, Windows and sanitizer checks must pass. Performance claims must include
the hardware, build configuration, workload, measurement method and raw results.

## Security

Follow [SECURITY.md](SECURITY.md). Never place vulnerability details, private
keys, wallet seeds, credentials or real user data in issues, commits or tests.

## Licensing contributions

By submitting a contribution, you confirm that you have the right to submit it
and expressly offer the rights you control in that contribution under the
[Onuros Community Software License 1.0](LICENSE). Clearly identify third-party
material and preserve all required notices.
