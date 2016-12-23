cmake_minimum_required(VERSION 2.6)
project(owondump)

include(FindPkgConfig)

pkg_search_module(LIBUSB REQUIRED libusb)

add_executable(owondump owondump.c)
add_executable(owonfileread owonfileread.c)
target_include_directories(owondump SYSTEM PUBLIC ${LIBUSB_INCLUDE_DIRS})
target_link_libraries(owondump ${LIBUSB_LIBRARIES})
