cmake_minimum_required(VERSION 3.22)
project(qmodemhelper)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_program(CTAGS ctags)
find_program(CSCOPE cscope)

find_package (PkgConfig REQUIRED)
find_package(LibXml2 REQUIRED)

#add_compile_options(-Wall -Wextra -Werror -O1)
pkg_check_modules (MM-GLIB REQUIRED mm-glib)
pkg_check_modules (MBIM-GLIB REQUIRED mbim-glib)

add_compile_options(-Wall -Wextra  -O1 )
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/qdl
  ${LIBXML2_INCLUDE_DIR}
  ${MM-GLIB_INCLUDE_DIRS}
  ${MBIM-GLIB_INCLUDE_DIRS}
  )

add_custom_target(cscope.files
  COMMAND find . -maxdepth 1 -name "*.c" -o -name "*.h" > cscope.files
  COMMAND cscope -q -R -b -i cscope.files
  VERBATIM
  )

add_executable(qmodemhelper
  ql-modem-helper.c
  ql-mbim-core.h
  ql-mbim-core.c
  ql-sahara-core.h
  ql-sahara-core.c
  ql-gpio.h 
  ql-gpio.c
  )

target_link_libraries(qmodemhelper udev gpiod ${LIBXML2_LIBRARIES}  ${MM-GLIB_LIBRARIES} ${MBIM-GLIB_LIBRARIES})

install (TARGETS qmodemhelper RUNTIME DESTINATION bin)
