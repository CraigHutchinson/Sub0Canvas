cmake_minimum_required(VERSION 3.24)
# Sub0Canvas: the layered document's C++ library (lib/) beside its static viewer (index.html, src/*.js). The viewer has no build step; this file builds
# only the library and its tests. Sub0Squad consumes the library through CPM (CPMAddPackage NAME Sub0Canvas), pinned to a tag.
project(sub0canvas VERSION 0.3.0 LANGUAGES CXX)

set(SUB0CANVAS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(SUB0CANVAS_TOP_LEVEL ON)
endif()
option(SUB0CANVAS_BUILD_TESTING "Build Sub0Canvas's tests" ${SUB0CANVAS_TOP_LEVEL})

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
add_library(sub0canvas STATIC lib/src/outline.cpp lib/src/edits.cpp lib/src/canvas.cpp lib/src/doc_layers.cpp lib/src/content_id.cpp lib/src/turn_control.cpp)
add_library(sub0canvas::sub0canvas ALIAS sub0canvas)
target_compile_features(sub0canvas PUBLIC cxx_std_23)
target_include_directories(sub0canvas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/src)
target_compile_options(sub0canvas PRIVATE ${SUB0CANVAS_STRICT})
target_link_libraries(sub0canvas PRIVATE simdjson::simdjson)

# The command line (app/): the I/O the pure library may not do, such as reading a git repository for `sub0canvas snapshot`.
option(SUB0CANVAS_BUILD_APP "Build the sub0canvas command line" ${SUB0CANVAS_TOP_LEVEL})
if(SUB0CANVAS_BUILD_APP)
  add_executable(sub0canvas_app app/sub0canvas.cpp)
  set_target_properties(sub0canvas_app PROPERTIES OUTPUT_NAME sub0canvas)
  target_compile_options(sub0canvas_app PRIVATE ${SUB0CANVAS_STRICT})
  target_include_directories(sub0canvas_app PRIVATE lib/src)     # json_util.hpp: one JSON writer for the library and its tools
  target_link_libraries(sub0canvas_app PRIVATE sub0canvas simdjson::simdjson)
endif()

if(SUB0CANVAS_BUILD_TESTING)
  enable_testing()
  # The test helper (sub0platform/testing/check.hpp) comes from Sub0Platform. Only the tests link it: the library and the viewer gain no dependency.
  # The pin is main's commit until Sub0Platform is tagged; then it moves to the tag.
  if(NOT TARGET Sub0Platform::sub0platform)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/get_cpm.cmake)
    CPMAddPackage(NAME Sub0Platform
      GIT_REPOSITORY https://github.com/CraigHutchinson/Sub0Platform.git
      GIT_TAG 5feccdf226046cc790214bc30793832a1888ce4b
      SOURCE_SUBDIR cpp)
  endif()
  # Requirement tests (docs/REQUIREMENTS.md): each names the requirements it checks; doc_driver.hpp keeps them independent of the library's internals.
  foreach(name test_edits test_doc_layers test_views test_canvas_model test_golden_replay test_replies test_turn_control)
    add_executable(${name} lib/tests/${name}.cpp)
    target_compile_options(${name} PRIVATE ${SUB0CANVAS_STRICT} -g)
    target_include_directories(${name} PRIVATE lib/tests)
    target_link_libraries(${name} PRIVATE sub0canvas simdjson::simdjson Sub0Platform::sub0platform)
    set(args "")
    if(name STREQUAL "test_golden_replay")
      set(args ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/golden)    # real producer output
    elseif(name STREQUAL "test_replies")
      set(args ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/replies)   # recorded reply shapes
    endif()
    add_test(NAME ${name} COMMAND ${name} ${args})
  endforeach()
  # The viewer's tests (test/*.test.js: the model and the demo fixtures) run under node when it is installed, so CI covers both halves.
  find_program(SUB0CANVAS_NODE node)
  if(SUB0CANVAS_NODE)
    file(GLOB viewerTests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test/*.test.js)
    add_test(NAME test_viewer COMMAND ${SUB0CANVAS_NODE} --test ${viewerTests} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    if(SUB0CANVAS_BUILD_APP)
      set_tests_properties(test_viewer PROPERTIES ENVIRONMENT "SUB0CANVAS_EXE=$<TARGET_FILE:sub0canvas_app>")   # test/snapshot.test.js runs it
    endif()
  else()
    message(STATUS "Sub0Canvas: node not found; the viewer's tests (test_viewer) are not registered")
  endif()
  # Test tiers (CraigHutchinson/.github docs/ci-strategy.md): every test is fast, so all are smoke; `ctest --preset quick` excludes only the label
  # full, which no test carries yet. A long suite added later is labelled full.
  get_property(sub0canvasTests DIRECTORY PROPERTY TESTS)
  set_tests_properties(${sub0canvasTests} PROPERTIES LABELS smoke)

  # The consumer rung (docs/MOCK_SQUAD.md, docs/TESTING.md): consumer/mock_squad is a separate CMake project that fetches this checkout through CPM, exactly as
  # Sub0Squad does, and links only the public target. `ctest --build-and-test` configures, builds and runs it; CPM_Sub0Canvas_SOURCE points it at this source tree,
  # so what it proves is this checkout as a consumer sees it. Labelled full: it builds the library a second time (see the measured cost in docs/MOCK_SQUAD.md).
  option(SUB0CANVAS_BUILD_CONSUMER_TESTS "Register the mock squad consumer project as a test (test_consumer_mock_squad)" ${SUB0CANVAS_TOP_LEVEL})
  if(SUB0CANVAS_BUILD_CONSUMER_TESTS)
    set(consumerOptions -DCPM_Sub0Canvas_SOURCE=${CMAKE_CURRENT_SOURCE_DIR} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCMAKE_BUILD_TYPE=Release)
    if(DEFINED Sub0Platform_SOURCE_DIR)
      list(APPEND consumerOptions -DCPM_Sub0Platform_SOURCE=${Sub0Platform_SOURCE_DIR})   # the pin this build already resolved
    endif()
    if(DEFINED CPM_SOURCE_CACHE AND NOT "${CPM_SOURCE_CACHE}" STREQUAL "")
      list(APPEND consumerOptions -DCPM_SOURCE_CACHE=${CPM_SOURCE_CACHE})                # the global cache, never one under out/ or build/
    endif()
    add_test(NAME test_consumer_mock_squad
      COMMAND ${CMAKE_CTEST_COMMAND} --build-and-test ${CMAKE_CURRENT_SOURCE_DIR}/consumer/mock_squad ${CMAKE_CURRENT_BINARY_DIR}/consumer_mock_squad
              --build-generator ${CMAKE_GENERATOR} --build-makeprogram ${CMAKE_MAKE_PROGRAM} --build-noclean
              --build-options ${consumerOptions}
              --test-command ${CMAKE_CTEST_COMMAND} --output-on-failure)
    set_tests_properties(test_consumer_mock_squad PROPERTIES LABELS full TIMEOUT 900)
  endif()
endif()
