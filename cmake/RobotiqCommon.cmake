# Code shared by the SDKs of robotiq-sdk lives in common/<module>/, one
# directory per module, each with a CMakeLists.txt defining
# robotiq_common_<module> (alias Robotiq::common::<module>): an INTERFACE
# target that carries the module's sources and include directories.
#
#   include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/RobotiqCommon.cmake)
#   robotiq_link_common(<target> <module>...)
#
# compiles each module's sources into <target>, privately. A module is never
# a library of its own, so an SDK neither installs nor exports it, and its
# consumers never see it: the shared code stays an implementation detail of
# every SDK that uses it. Each module is added once per build, however many
# SDKs of the same build (dev/CMakeLists.txt) ask for it.

get_filename_component(ROBOTIQ_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)

function(robotiq_link_common target)
  foreach(module IN LISTS ARGN)
    if(NOT TARGET robotiq_common_${module})
      set(module_dir "${ROBOTIQ_SDK_ROOT}/common/${module}")
      if(NOT EXISTS "${module_dir}/CMakeLists.txt")
        message(FATAL_ERROR "robotiq_link_common: no module '${module}' in ${ROBOTIQ_SDK_ROOT}/common")
      endif()
      add_subdirectory("${module_dir}" "${CMAKE_BINARY_DIR}/robotiq_common/${module}")
    endif()
    # BUILD_INTERFACE: should <target> be installed and exported, the module
    # stays out of its export set, where it has no artifact to resolve to.
    target_link_libraries(${target} PRIVATE $<BUILD_INTERFACE:robotiq_common_${module}>)
  endforeach()
endfunction()
