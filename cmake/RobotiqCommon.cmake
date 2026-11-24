# robotiq_link_common(<target> <module>...): compiles common/ modules into
# <target>. See common/README.md.

get_filename_component(ROBOTIQ_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
include("${CMAKE_CURRENT_LIST_DIR}/RobotiqSdkVersion.cmake")

# Every module's headers target links it.
if(NOT TARGET robotiq_common_abi)
  add_library(robotiq_common_abi INTERFACE)
  string(REPLACE "." "_" _robotiq_common_abi "v${ROBOTIQ_SDK_VERSION}")
  target_include_directories(robotiq_common_abi INTERFACE "${ROBOTIQ_SDK_ROOT}/common/include")
  target_compile_definitions(robotiq_common_abi INTERFACE ROBOTIQ_COMMON_ABI=${_robotiq_common_abi})
endif()

function(robotiq_link_common target)
  foreach(module IN LISTS ARGN)
    if(NOT TARGET robotiq_common_${module})
      set(module_dir "${ROBOTIQ_SDK_ROOT}/common/${module}")
      if(NOT EXISTS "${module_dir}/CMakeLists.txt")
        message(FATAL_ERROR "robotiq_link_common: no module '${module}' in ${ROBOTIQ_SDK_ROOT}/common")
      endif()
      add_subdirectory("${module_dir}" "${CMAKE_BINARY_DIR}/robotiq_common/${module}")
    endif()
    # Keeps the module out of <target>'s export set.
    target_link_libraries(${target} PRIVATE $<BUILD_INTERFACE:robotiq_common_${module}>)
  endforeach()
endfunction()
