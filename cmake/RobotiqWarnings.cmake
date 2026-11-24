# robotiq_add_warnings_target(<name> AS_ERRORS <bool>): an INTERFACE target
# with the warnings every C++ target here builds with. Link it privately.
# A second call with the same name must pass the same AS_ERRORS.
#
# MSVC's -Wall is /Wall, unbuildable under /WX. /utf-8: the sources hold
# UTF-8 string literals.

function(robotiq_add_warnings_target name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "AS_ERRORS" "")
  if(arg_AS_ERRORS)
    set(as_errors TRUE)
  else()
    set(as_errors FALSE)
  endif()
  if(TARGET ${name})
    get_property(previous GLOBAL PROPERTY ROBOTIQ_WARNINGS_AS_ERRORS_${name})
    if(NOT previous STREQUAL as_errors)
      message(FATAL_ERROR "${name} already exists with AS_ERRORS ${previous}")
    endif()
    return()
  endif()
  set_property(GLOBAL PROPERTY ROBOTIQ_WARNINGS_AS_ERRORS_${name} ${as_errors})
  if(MSVC)
    set(flags /W4 /permissive- /utf-8)
    set(error_flag /WX)
  else()
    set(flags -Wall -Wextra)
    set(error_flag -Werror)
  endif()
  if(as_errors)
    list(APPEND flags ${error_flag})
  endif()
  add_library(${name} INTERFACE)
  target_compile_options(${name} INTERFACE $<$<COMPILE_LANGUAGE:CXX>:${flags}>)
endfunction()
