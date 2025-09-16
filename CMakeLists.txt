cmake_minimum_required(VERSION 3.31)
project(curl_m3u8)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_BUILD_TYPE Debug) # Change to Release if you want a release-build.

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-Wall)
  #add_compile_options(-fno-omit-frame-pointer -fsanitize=address)
  #add_link_options(-fno-omit-frame-pointer -fsanitize=address)
  #add_compile_definitions(...)
endif()

set(CMAKE_VERBOSE_MAKEFILE ON)
# or make VERBOSE=1

find_package(CURL REQUIRED)

add_executable(curl_m3u8 main.cc curl_wrapper.cc progressmeter.cc m3u8.cc)
target_link_libraries(curl_m3u8 CURL::libcurl)

add_executable(progressmeter_check progressmeter_check.cc progressmeter.cc)

add_executable(m3u8_check m3u8_check.cc m3u8.cc)

# ---

find_package(GTest REQUIRED)
add_executable(testrunner progressmeter_test.cc progressmeter.cc m3u8_test.cc m3u8.cc string_util_test.cc)
target_link_libraries(testrunner GTest::GTest GTest::Main)

add_custom_target(test
  COMMAND testrunner
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  VERBATIM
)

# ---

add_custom_target(tags
  COMMAND ctags --languages=c++ --c++-kinds=+p --fields=+iaS --extras=+q -R .
    /usr/include/curl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  VERBATIM
)

add_custom_target(distclean
  COMMAND make clean
  COMMAND rm -rf CMakeFiles CMakeCache.txt cmake_install.cmake Makefile
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

