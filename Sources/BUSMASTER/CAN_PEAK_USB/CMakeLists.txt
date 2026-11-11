set(sources
  ../DIL_Interface/HardwareListingCAN.cpp
  CAN_PEAK_USB.cpp
  CAN_PEAK_USB_stdafx.cpp)

set(headers
  ../DIL_Interface/HardwareListingCAN.h
  CAN_PEAK_USB.h
  CAN_PEAK_USB_Extern.h
  CAN_PEAK_USB_stdafx.h
  PCANBasicDyn.h)

set(resources
  CAN_PEAK_USB.rc)

add_library(CAN_PEAK_USB SHARED ${sources} ${headers} ${resources})

include_directories(
  ..
  ${ICONV_INCLUDE_DIR}
  ${LIBXML2_INCLUDE_DIR}
  ${ATL_INCLUDE_DIRS}
  ${MFC_INCLUDE_DIRS})

# compiler options
add_definitions(-D_USRDLL)

# linker options (PCANBasic.dll of the PEAK device driver is loaded at run time: no import library)
set_target_properties(CAN_PEAK_USB PROPERTIES LINK_FLAGS "/NODEFAULTLIB:daouuid")
target_link_libraries(CAN_PEAK_USB
  DataTypes
  Utils)

# installer options
add_custom_command(
  TARGET CAN_PEAK_USB
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} ARGS -E make_directory ${PROJECT_SOURCE_DIR}/BIN/${CMAKE_BUILD_TYPE}/
  COMMAND ${CMAKE_COMMAND} ARGS -E copy $<TARGET_FILE:CAN_PEAK_USB> ${PROJECT_SOURCE_DIR}/BIN/${CMAKE_BUILD_TYPE}/)
