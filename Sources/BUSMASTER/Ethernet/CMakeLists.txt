set(sources
  Ethernet.cpp
  EthernetAdapterDlg.cpp
  EthernetCapture.cpp
  EthernetDecoder.cpp
  EthernetFrame.cpp
  EthernetMsgWnd.cpp
  EthernetTxWnd.cpp
  Ethernet_stdafx.cpp)

set(headers
  EthernetAdapterDlg.h
  EthernetCapture.h
  EthernetDecoder.h
  EthernetFrame.h
  EthernetModule.h
  EthernetMsgWnd.h
  EthernetTxWnd.h
  Ethernet_Extern.h
  Ethernet_Resource.h
  Ethernet_stdafx.h)

set(resources
  Ethernet.rc)

add_library(Ethernet SHARED ${sources} ${headers} ${resources})

include_directories(
  ..
  ${ATL_INCLUDE_DIRS}
  ${MFC_INCLUDE_DIRS})

# compiler options
add_definitions(-D_AFXEXT)

# linker options: Npcap / WinPcap (wpcap.dll) is loaded at run time
target_link_libraries(Ethernet
  iphlpapi
  winmm)

# installer options
add_custom_command(
  TARGET Ethernet
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} ARGS -E make_directory ${PROJECT_SOURCE_DIR}/BIN/${CMAKE_BUILD_TYPE}/
  COMMAND ${CMAKE_COMMAND} ARGS -E copy $<TARGET_FILE:Ethernet> ${PROJECT_SOURCE_DIR}/BIN/${CMAKE_BUILD_TYPE}/)
