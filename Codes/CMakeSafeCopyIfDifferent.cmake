if (NOT DEFINED SRC)
  message(FATAL_ERROR "CMakeSafeCopyIfDifferent requires SRC.")
endif()

if (NOT DEFINED DST)
  message(FATAL_ERROR "CMakeSafeCopyIfDifferent requires DST.")
endif()

string(REGEX REPLACE "^\"|\"$" "" SRC "${SRC}")
string(REGEX REPLACE "^\"|\"$" "" DST "${DST}")

if (IS_DIRECTORY "${DST}")
  set(_dst_dir "${DST}")
else()
  get_filename_component(_dst_dir "${DST}" DIRECTORY)
endif()

if (_dst_dir)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_dst_dir}"
    RESULT_VARIABLE _mkdir_result)
  if (NOT _mkdir_result EQUAL 0)
    message(WARNING "Could not create copy destination directory: ${_dst_dir}")
    return()
  endif()
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC}" "${DST}"
  RESULT_VARIABLE _copy_result
  ERROR_VARIABLE _copy_error)

if (NOT _copy_result EQUAL 0)
  message(WARNING
    "Could not copy ${SRC} to ${DST}. "
    "The destination is probably locked by a running editor/client. "
    "${_copy_error}")
endif()
