cmake_minimum_required(VERSION 3.24)
# sub0canvas: the layered document's C++ library (lib/) beside its static viewer (index.html, src/*.js). The viewer has no build step; this file builds
# only the library and its tests. Sub0Squad consumes the library through CPM (CPMAddPackage NAME sub0canvas), pinned to a tag.
project(sub0canvas VERSION 0.2.0 LANGUAGES CXX)

set(SUB0CANVAS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(SUB0CANVAS_TOP_LEVEL ON)
endif()
option(SUB0CANVAS_BUILD_TESTING "Build sub0canvas's tests" ${SUB0CANVAS_TOP_LEVEL})

if(SUB0CANVAS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# simdjson (the JSON writer behind the interchange files; kept out of public headers). A consumer that already has it (Sub0Squad) is not given a second.
if(NOT TARGET simdjson::simdjson)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/get_cpm.cmake)
  CPMAddPackage(NAME simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG 40a686ab89dc5ee58ec10d0514dd5c99abbc472b   # 5.1.1, the pin Sub0Squad uses
    OPTIONS "SIMDJSON_DEVELOPER_MODE OFF")
endif()

set(SUB0CANVAS_STRICT -Wall -Wextra -Werror)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # As in Sub0Squad: GCC 15's -Warray-bounds fires inside simdjson's json_escape.h after inlining, and -Wformat-truncation on small line-number buffers.
  list(APPEND SUB0CANVAS_STRICT -Wno-error=array-bounds -Wno-error=format-truncation)
endif()

# The library: folded views and READ requests (outline), search/replace and line edits (edits), the revisioned canvas and its transactions (canvas),
# stable block ids and layers (doc_layers).
add_library(sub0canvas STATIC lib/src/outline.cpp lib/src/edits.cpp lib/src/canvas.cpp lib/src/doc_layers.cpp)
add_library(sub0canvas::sub0canvas ALIAS sub0canvas)
target_compile_features(sub0canvas PUBLIC cxx_std_23)
target_include_directories(sub0canvas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/src)
target_compile_options(sub0canvas PRIVATE ${SUB0CANVAS_STRICT})
target_link_libraries(sub0canvas PRIVATE simdjson::simdjson)

if(SUB0CANVAS_BUILD_TESTING)
  enable_testing()
  foreach(name test_edits test_doc_layers)
    add_executable(${name} lib/tests/${name}.cpp)
    target_compile_options(${name} PRIVATE ${SUB0CANVAS_STRICT} -g)
    target_include_directories(${name} PRIVATE lib/tests)
    target_link_libraries(${name} PRIVATE sub0canvas simdjson::simdjson)
    add_test(NAME ${name} COMMAND ${name})
  endforeach()
endif()
