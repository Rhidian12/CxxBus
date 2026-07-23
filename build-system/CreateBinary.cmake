# Helper function to define a CMake target for a unit test
# Arguments:
# - NAME: The name of your CMake Target
# - SOURCES: List of source files to be compiled for the executable
# - LIBRARIES: List of libraries to be included for the executable
# - INCLUDE_DIRS: List of include directories to be included for the executable
function(create_binary)
  set(oneValueArgs NAME)
  set(multiValueArgs SOURCES LIBRARIES INCLUDE_DIRS)

  cmake_parse_arguments(CB
    "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  # Create our binary
  add_executable(${CB_NAME} EXCLUDE_FROM_ALL ${CB_SOURCES})
  target_link_libraries(${CB_NAME} PRIVATE ${CB_LIBRARIES})
  target_include_directories(${CB_NAME} PUBLIC ${CB_INCLUDE_DIRS})
endfunction()
