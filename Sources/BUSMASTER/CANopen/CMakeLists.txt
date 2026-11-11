set(sources
  CANopen.cpp
  CANopenDecoder.cpp
  CANopenMonitor.cpp
  CANopenWnd.cpp
  CANopenFrame.cpp
  CANopen_stdafx.cpp)

set(headers
  CANopenDecoder.h
  CANopenMonitor.h
  CANopenWnd.h
  CANopenFrame.h
  CANopen_Extern.h
  CANopen_Resource.h
  CANopen_stdafx.h)

set(resources
  CANopen.rc)

add_library(CANopen SHARED ${sources} ${headers} ${resources})

include_directories(
  ..
  ${ATL_INCLUDE_DIRS}
  ${MFC_INCLUDE_DIRS})

# compiler options
add_definitions(-D_AFXEXT)

# linker options
target_link_libraries(CANopen
  DIL_Interface)

# installer options
add_custom_command(
  TARGET CANopen
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} ARGS -E make_directory ${PROJECT_SOURCE_DIR}/BIN/${CMAKE_BUILD_TYPE}/
  COMMAND ${CMAKE_COMMAND} ARGS -E copy $<TARGET_FILE:CANopen> ${PROJECT_SOURCE_DIR}/BIN/${CMAKE_BUILD_TYPE}/)
