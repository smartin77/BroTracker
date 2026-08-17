# Testing

BroTracker uses a small, dependency-free unit test setup for the platform-independent tracker core (`src/core`). The tests do not require Teensy hardware and run entirely on the host.

## Test Architecture

* `BroTrackerCore` — a static library containing the shared core logic (`logger`, `tune_loader`, `note_formatter`, `note_names`). It is linked by both the UI executable and the test executable so the core is only compiled once.
* `BroTrackerTests` — a separate executable (`tests/`) that links `BroTrackerCore` and runs all registered test cases.
* `tests/test_framework.h` — a minimal, self-contained test harness (no external dependency). It provides:
  * `TEST_CASE(name) { ... }` — declares and auto-registers a test.
  * `CHECK(expression)` — reports a failure if the expression is false.
  * `CHECK_EQ(actual, expected)` — reports a failure if the two values are not equal.

## Prerequisites

The project must be configured once with CMake (Ninja generator), matching the existing `CMake Build and Run` task:

```powershell
cmake -S . -B build -G Ninja
```

This only needs to be repeated if the build directory is deleted or the CMake toolchain changes.

## Running Tests in VS Code

### Option A — Task (recommended)

1. Open the Command Palette (`Ctrl+Shift+P`).
2. Run **Tasks: Run Test Task**.
3. VS Code runs the **"CMake Build and Test"** task, which builds the project and then runs `ctest`.

Test results (pass/fail summary) appear in the integrated terminal.

### Option B — Integrated terminal

From the workspace root:

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

`--output-on-failure` prints the full test output only when a test fails, keeping successful runs concise.

### Option C — Run the test binary directly

For a detailed, per-test-case breakdown instead of the summarized `ctest` output:

```powershell
./build/BroTrackerTests.exe
```

This prints a `[RUN]` / `[ OK ]` / `[FAIL]` line for every `TEST_CASE`, followed by a final pass/fail count.

> Tests that load files (e.g. `assets/dummy_my_tune.json`) assume the working directory is the repository root. `ctest` is configured with `WORKING_DIRECTORY` set to the repo root, so running through `ctest` or the VS Code task always resolves relative asset paths correctly. Running `BroTrackerTests.exe` directly only works correctly if your terminal's current directory is the repository root.

## Adding New Tests

1. Add a new `tests/test_<area>.cpp` file, or add cases to an existing one.
2. Include `test_framework.h` and the core header(s) under test.
3. Write one or more `TEST_CASE(...)` blocks using `CHECK`/`CHECK_EQ`.
4. Register the new `.cpp` file in `CMakeLists.txt` under the `BroTrackerTests` executable's source list.
5. Reconfigure (`cmake -S . -B build -G Ninja`) so CMake picks up the new source file, then build and run tests as above.
