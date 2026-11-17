# robotiq_add_warnings_target(<name> <as_errors>): an INTERFACE target with
# the warnings every C++ target here builds with. Link it privately.
#
# MSVC's -Wall is /Wall, unbuildable under /WX. /utf-8: the sources hold
# UTF-8 string literals.

function(robotiq_add_warnings_target name as_errors)
  if(TARGET ${name})
    return()
  endif()
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
