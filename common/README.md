# common

Code shared by the SDKs of this repository. It is internal: no module here is a library of its own, and none is installed or exported. Each SDK compiles the modules it uses into its own library and never includes them from its public headers, so its users see only that SDK's API.

| Module | What it holds | Used by |
|--------|---------------|---------|
| [serial/](serial/) | The OS serial transport: `SerialPort` (termios on Linux and macOS, the Win32 comm API on Windows) and the `SerialError` it throws | grippers, tsf |
| [latency_timer/](latency_timer/) | The Linux FTDI `latency_timer`, set through sysfs, for SDKs whose devices sit behind an FTDI USB-serial adapter | grippers |

## Using a module

From an SDK's CMake project:

```cmake
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/RobotiqCommon.cmake")
robotiq_link_common(<target> serial)
```

`robotiq_link_common` adds the module once per build, however many SDKs ask for it (the [dev/](../dev/) aggregate builds them all together), and compiles its sources into `<target>` privately, with that target's flags. `$<BUILD_INTERFACE:>` keeps the module out of `<target>`'s export set, where it has no artifact to resolve to.

Each SDK gets its own copy of a module's code. A program that links two SDKs therefore holds two copies of the `Robotiq::common` symbols: the static archives resolve them silently to one, which is harmless while both SDKs come from the same repository version, as their identical sources guarantee. Mixing SDK builds of different versions in one program is not supported.

## Adding a module

- One folder per module: `common/<module>/`, with its headers under `include/Robotiq/common/` and its sources under `src/`. [serial/](serial/) is the model.
- Its `CMakeLists.txt` defines two INTERFACE targets:
  - `robotiq_common_<module>` (alias `Robotiq::common::<module>`): the module's sources, for `robotiq_link_common`;
  - `robotiq_common_<module>_headers` (alias `Robotiq::common::<module>_headers`): the include directory alone, for code that calls the module without compiling it, such as an SDK's unit tests linking the SDK's library.
- The code lives in namespace `Robotiq::common`, is C++17, and reports errors with its own exception types: each SDK turns them into its own error reporting at its boundary.
- Its unit tests sit in `common/<module>/tests/`, added to [CMakeLists.txt](CMakeLists.txt) here, and link `robotiq_common_warnings`, so the module's sources build with the SDKs' warnings as errors (`cmake/RobotiqWarnings.cmake`). Test support other suites reuse goes in a header-only target of its own, as `serial/`'s `Robotiq::common::serial_testing` (a pseudo-terminal peer, namespace `Robotiq::common::serial_testing`).

## Tests

```sh
cmake -S common -B build/common -DCMAKE_BUILD_TYPE=Release
cmake --build build/common --config Release
ctest --test-dir build/common -C Release
```

CI runs them on Linux, macOS and Windows, builds the `dev/` aggregate, and checks the format (`.github/workflows/common.yml`).
