set(sources
  ../DIL_Interface/HardwareListingCAN.cpp
  CAN_Teensy.cpp
  CAN_Teensy_stdafx.cpp
  TeensyLink.cpp)

set(headers
  CAN_Teensy.h
  CAN_Teensy_Extern.h
  CAN_Teensy_stdafx.h
  TeensyLink.h
  TeensyProtocol.h)

set(resources
  CAN_Teensy.rc)

add_library(CAN_Teensy SHARED ${sources} ${headers} ${resources})

include_directories(
  ..
  ${ICONV_INCLUDE_DIR}
  ${LIBXML2_INCLUDE_DIR}
  ${ATL_INCLUDE_DIRS}
  ${MFC_INCLUDE_DIRS})

# compiler options
add_definitions(-D_USRDLL)

# linker options
set_target_properties(CAN_Teensy PROPERTIES LINK_FLAGS "/NODEFAULTLIB:daouuid")
target_link_libraries(CAN_Teensy
  DataTypes
  Utils
  setupapi)

# installer options
add_custom_command(
  TARGET CAN_Teensy
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} ARGS -E make_directory ${PROJECT_SOURCE_DIR}/BIN/${CMAKE_BUILD_TYPE}/
  COMMAND ${CMAKE_COMMAND} ARGS -E copy $<TARGET_FILE:CAN_Teensy> ${PROJECT_SOURCE_DIR}/BIN/${CMAKE_BUILD_TYPE}/)
