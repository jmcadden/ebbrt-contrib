cmake_minimum_required(VERSION 2.6 FATAL_ERROR)
project("socket_demo-ebbrt" C CXX)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
set(CMAKE_CXX_FLAGS_DEBUG          "-O0 -g3")
set(CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE        "-O4 -flto -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g3")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++14 -Wall -Werror")

set(HOSTED_SOURCES
      src/hosted/socket_demo.cc
      src/hosted/Printer.cc)

set(BAREMETAL_SOURCES
      src/baremetal/socket_demo.cc
      src/baremetal/Printer.cc)

# Baremetal  ========================================================
if( ${CMAKE_SYSTEM_NAME} STREQUAL "EbbRT")
  find_package(EbbRTSocket REQUIRED)
  include_directories(${EBBRT-SOCKET_INCLUDE_DIRS})
  add_executable(socket_demo.elf ${BAREMETAL_SOURCES} )
  target_link_libraries(socket_demo.elf ${EBBRT-SOCKET_LIBRARIES})
  add_custom_command(TARGET socket_demo.elf POST_BUILD
    COMMAND objcopy -O elf32-i386 socket_demo.elf socket_demo.elf32 )

# Hosted  ===========================================================
elseif( ${CMAKE_SYSTEM_NAME} STREQUAL "Linux" )
  find_package(EbbRT REQUIRED)
  find_package(Boost 1.53.0 REQUIRED COMPONENTS
    filesystem system coroutine context )
  find_package(Capnp REQUIRED)
  find_package(TBB REQUIRED)
  find_package(Threads REQUIRED)

  include_directories(${EBBRT_INCLUDE_DIRS})
  add_executable(socket_demo ${HOSTED_SOURCES})
  target_link_libraries(socket_demo ${EBBRT_LIBRARIES}
    ${CAPNP_LIBRARIES_LITE} ${CMAKE_THREAD_LIBS_INIT}
    ${Boost_LIBRARIES} ${TBB_LIBRARIES}
  )
else()
  message(FATAL_ERROR "System name unsupported: ${CMAKE_SYSTEM_NAME}")
endif()
