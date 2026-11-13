# common

Code shared by the SDKs of this repository. It is internal: no module here is a library of its own, and none is installed or exported. Each SDK compiles the modules it uses into its own library and never includes them from its public headers, so its users see only that SDK's API.

| Module | What it holds | Used by |
|--------|---------------|---------|
| [serial/](serial/) | The OS serial transport: `SerialPort` (termios on Linux and macOS, the Win32 comm API on Windows), the `SerialError` it throws, and the Linux FTDI `latency_timer` helpers | grippers, tsf |

## Using a module

From an SDK's CMake project:

```cmake
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/RobotiqCommon.cmake")
robotiq_link_common(<target> serial)
```

`robotiq_link_common` adds the module once per build, however many SDKs ask for it, and compiles its sources into `<target>` privately, with that target's flags. See [cmake/RobotiqCommon.cmake](../cmake/RobotiqCommon.cmake).

## Adding a module

- One folder per module: `common/<module>/`, with its headers under `include/Robotiq/common/` and its sources under `src/`.
- Its `CMakeLists.txt` defines `robotiq_common_<module>`, an INTERFACE target holding the module's sources and include directory, aliased `Robotiq::common::<module>`. [serial/CMakeLists.txt](serial/CMakeLists.txt) is the model.
- The code lives in namespace `Robotiq::common`, is C++17, and reports errors with its own exception types: each SDK turns them into its own error reporting at its boundary.
