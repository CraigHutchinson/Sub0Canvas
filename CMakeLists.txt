cmake_minimum_required(VERSION 3.24)
# Sub0Canvas: the layered document's C++ library (lib/) beside its static viewer (index.html, src/*.js). The viewer has no build step; this file builds
# only the library and its tests. Sub0Squad consumes the library through CPM (CPMAddPackage NAME Sub0Canvas), pinned to a tag.
project(sub0canvas VERSION 0.4.0 LANGUAGES CXX)

set(SUB0CANVAS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(SUB0CANVAS_TOP_LEVEL ON)
endif()
option(SUB0CANVAS_BUILD_TESTING "Build Sub0Canvas's tests" ${SUB0CANVAS_TOP_LEVEL})
option(SUB0CANVAS_BUILD_BENCHMARKS "Build the scale harness (bench/): the synthetic corpus generator and the benchmarks (Sub0Canvas_Bench)" ${SUB0CANVAS_TOP_LEVEL})

if(SUB0CANVAS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/print_link.cmake)   # std::println links on every toolchain
endif()

# simdjson (the JSON writer behind the interchange files; kept out of public headers). A consumer that already has it (Sub0Squad) is not given a second.
if(NOT TARGET simdjson::simdjson)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/get_cpm.cmake)
  CPMAddPackage(NAME simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG v5.1.1   # third party: a readable tag (the version Sub0Squad uses)
    OPTIONS "SIMDJSON_DEVELOPER_MODE OFF")
endif()

set(SUB0CANVAS_STRICT -Wall -Wextra -Werror)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # As in Sub0Squad: GCC 15's -Warray-bounds fires inside simdjson's json_escape.h after inlining, and -Wformat-truncation on small line-number buffers.
  list(APPEND SUB0CANVAS_STRICT -Wno-error=array-bounds -Wno-error=format-truncation)
endif()

# The library: folded views and READ requests (outline), search/replace and line edits (edits), the revisioned canvas and its transactions (canvas),
# stable block ids and layers (doc_layers), the event envelope and log seam (event) and the store core folded from events (store).
add_library(sub0canvas STATIC lib/src/outline.cpp lib/src/blocks.cpp lib/src/edits.cpp lib/src/canvas.cpp lib/src/doc_layers.cpp lib/src/content_id.cpp lib/src/turn_control.cpp
            lib/src/git_plumbing.cpp lib/src/revision_index.cpp lib/src/event.cpp lib/src/store.cpp lib/src/claims.cpp lib/src/inbox.cpp lib/src/anchor.cpp lib/src/resolver.cpp lib/src/query.cpp lib/src/view.cpp lib/src/session.cpp)
add_library(sub0canvas::sub0canvas ALIAS sub0canvas)
target_compile_features(sub0canvas PUBLIC cxx_std_23)
target_include_directories(sub0canvas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/src)
target_compile_options(sub0canvas PRIVATE ${SUB0CANVAS_STRICT})
target_link_libraries(sub0canvas PRIVATE simdjson::simdjson)

# The command line (app/): the I/O the pure library may not do, such as reading a git repository for `sub0canvas snapshot`.
# The file log (app/file_log.*, B1b): the I/O half of the log seam, a library of its own so the tests can drive it without the command line.
if(SUB0CANVAS_TOP_LEVEL OR SUB0CANVAS_BUILD_TESTING)
  add_library(sub0canvas_io STATIC app/file_log.cpp)
  target_compile_features(sub0canvas_io PUBLIC cxx_std_23)
  target_include_directories(sub0canvas_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/app PRIVATE lib/src)   # lib/src: json_util.hpp
  target_compile_options(sub0canvas_io PRIVATE ${SUB0CANVAS_STRICT})
  target_link_libraries(sub0canvas_io PUBLIC sub0canvas PRIVATE simdjson::simdjson)
endif()
if(SUB0CANVAS_TOP_LEVEL)
  add_executable(sub0canvas_app app/sub0canvas.cpp app/git_source.cpp app/history.cpp)
  set_target_properties(sub0canvas_app PROPERTIES OUTPUT_NAME sub0canvas)
  target_compile_options(sub0canvas_app PRIVATE ${SUB0CANVAS_STRICT})
  target_include_directories(sub0canvas_app PRIVATE lib/src)     # json_util.hpp: one JSON writer for the library and its tools
  target_link_libraries(sub0canvas_app PRIVATE sub0canvas sub0canvas_io simdjson::simdjson)
endif()

# The scale harness (bench/, docs/TESTING.md "The synthetic corpus"): the corpus generator is pure and links nothing; the tests use it as the truth for block identity.
if(SUB0CANVAS_BUILD_TESTING OR SUB0CANVAS_BUILD_BENCHMARKS)
  add_library(sub0canvas_bench_corpus STATIC bench/corpus.cpp)
  target_compile_features(sub0canvas_bench_corpus PUBLIC cxx_std_23)
  target_include_directories(sub0canvas_bench_corpus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})   # #include "bench/corpus.hpp"
  target_compile_options(sub0canvas_bench_corpus PRIVATE ${SUB0CANVAS_STRICT})
  # The shared deterministic generator (bench/rng.hpp): header-only, for the corpus and the tests, never the library (which draws nothing).
  add_library(sub0canvas_rng INTERFACE)
  target_compile_features(sub0canvas_rng INTERFACE cxx_std_23)
  target_include_directories(sub0canvas_rng INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})   # #include "bench/rng.hpp"
  target_link_libraries(sub0canvas_bench_corpus PUBLIC sub0canvas_rng)
endif()

# The benchmarks (bench/bench_canvas.cpp) run through Sub0Perf's tooling (docs/TESTING.md, "Performance"): Sub0Canvas_Bench, plus the _json, _runs, _check,
# _baseline, _compare and _metrics targets. Timing is measured locally only (never in CI); the deterministic metrics (perf/metrics.json) and the smoke run are
# ctest tests labelled smoke, so they run in every tier. Sub0Perf is the family's performance tooling, moved out of Sub0Math (ADR 0004); DOWNLOAD_ONLY.
if(SUB0CANVAS_BUILD_BENCHMARKS)
  if(SUB0CANVAS_BUILD_TESTING)
    enable_testing()   # before the first add_test below (the testing block calls it again, harmlessly)
  endif()
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/get_cpm.cmake)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/sub0_deps_pinned.cmake)
  CPMAddPackage(NAME Sub0Perf
    GIT_REPOSITORY https://github.com/CraigHutchinson/Sub0Perf.git
    GIT_TAG ${SUB0_PIN_Sub0Perf}
    DOWNLOAD_ONLY YES)
  # No benchmark run after every build (run the targets deliberately). The pre-push gate follows Sub0Perf's default (SUB0PERF_GIT_HOOKS=AUTO, set -DSUB0PERF_GIT_HOOKS=OFF to skip it): installed
  # only from the main checkout, where this machine has a committed baseline (perf/baselines/<host>.json; SUB0PERF_HOST names another host), never on CI.
  set(SUB0PERF_ON_BUILD "OFF" CACHE STRING "")
  include("${Sub0Perf_SOURCE_DIR}/cmake/Sub0Perf.cmake")

  sub0perf_add_benchmark(Sub0Canvas_Bench bench/bench_canvas.cpp bench/bench_history.cpp bench/bench_store.cpp bench/bench_claims.cpp bench/bench_inbox.cpp bench/bench_resolver.cpp bench/bench_query.cpp bench/bench_session.cpp METRICS perf/metrics.json SMOKE_TEST)
  target_include_directories(Sub0Canvas_Bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(Sub0Canvas_Bench PRIVATE ${SUB0CANVAS_STRICT})
  target_link_libraries(Sub0Canvas_Bench PRIVATE sub0canvas sub0canvas_bench_corpus)
  if(MINGW)
    # The bench replaces operator new to count allocations and live bytes; MinGW's libstdc++ DLL would allocate strings with its own operator new and the
    # executable free them with ours (heap corruption, 0xC0000374). Linking the runtime statically keeps one allocator.
    target_link_options(Sub0Canvas_Bench PRIVATE -static-libstdc++ -static-libgcc)
  endif()
  # The timing run as a test, for `ctest --preset timing` (local only: label timing, which the quick and full presets exclude): interleaved rounds, then the
  # judgement against perf/baselines/<host>.json. `_smoke` and `_metrics` (label smoke) are registered by sub0perf_add_benchmark.
  if(SUB0CANVAS_BUILD_TESTING AND TARGET Sub0Canvas_Bench_check)
    add_test(NAME Sub0Canvas_Bench_timing COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target Sub0Canvas_Bench_check)
    set_tests_properties(Sub0Canvas_Bench_timing PROPERTIES LABELS timing TIMEOUT 3600 RUN_SERIAL TRUE)
  endif()
endif()

if(SUB0CANVAS_BUILD_TESTING)
  enable_testing()
  # The test helper (sub0platform/testing/check.hpp) comes from Sub0Platform. Only the tests link it: the library and the viewer gain no dependency.
  # The ref is in cmake/sub0_deps_pinned.cmake; CPM_Sub0Platform_SOURCE (a local checkout) overrides it.
  if(NOT TARGET Sub0Platform::sub0platform)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/get_cpm.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/sub0_deps_pinned.cmake)
    CPMAddPackage(NAME Sub0Platform
      GIT_REPOSITORY https://github.com/CraigHutchinson/Sub0Platform.git
      GIT_TAG ${SUB0_PIN_Sub0Platform}
      SOURCE_SUBDIR cpp)
  endif()
  # Requirement tests (docs/REQUIREMENTS.md): each names the requirements it checks; doc_driver.hpp keeps them independent of the library's internals.
  foreach(name test_edits test_doc_layers test_views test_outline test_canvas_model test_golden_replay test_replies test_turn_control test_revision_index test_store test_claims test_inbox test_file_log test_query test_views_sessions)
    add_executable(${name} lib/tests/${name}.cpp)
    target_compile_options(${name} PRIVATE ${SUB0CANVAS_STRICT} -g)
    target_include_directories(${name} PRIVATE lib/tests)
    target_link_libraries(${name} PRIVATE sub0canvas simdjson::simdjson Sub0Platform::sub0platform)
    if(name STREQUAL "test_canvas_model")
      target_include_directories(${name} PRIVATE lib/src)   # scan.hpp: its line helpers are checked against the tests' independent ones
    endif()
    if(name STREQUAL "test_file_log")
      target_link_libraries(${name} PRIVATE sub0canvas_io)
    endif()
    if(name STREQUAL "test_canvas_model" OR name STREQUAL "test_inbox")
      target_link_libraries(${name} PRIVATE sub0canvas_rng)   # the shared deterministic generator (bench/rng.hpp), not <random>
    endif()
    set(args "")
    if(name STREQUAL "test_golden_replay")
      set(args ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/golden)    # real producer output
    elseif(name STREQUAL "test_replies")
      set(args ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/replies)   # recorded reply shapes
    elseif(name STREQUAL "test_query")
      set(args ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)           # the mock attempts (fixtures/mock): the bundle the query answers must reproduce
    elseif(name STREQUAL "test_revision_index")
      set(args ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)           # captured git plumbing (fixtures/git) and its hand-checked truth (fixtures/scenarios/g1-tinycalc)
    endif()
    add_test(NAME ${name} COMMAND ${name} ${args})
  endforeach()
  # The corpus generator's test: determinism, counts, the blocks the library finds in what it generates (bench/corpus.hpp).
  add_executable(test_corpus lib/tests/test_corpus.cpp)
  target_compile_options(test_corpus PRIVATE ${SUB0CANVAS_STRICT} -g)
  target_link_libraries(test_corpus PRIVATE sub0canvas sub0canvas_bench_corpus simdjson::simdjson Sub0Platform::sub0platform)
  add_test(NAME test_corpus COMMAND test_corpus)
  # The shared generator (bench/rng.hpp): pinned outputs captured from the corpus's own generator before it moved.
  add_executable(test_rng lib/tests/test_rng.cpp)
  target_compile_options(test_rng PRIVATE ${SUB0CANVAS_STRICT} -g)
  target_link_libraries(test_rng PRIVATE sub0canvas_rng Sub0Platform::sub0platform)
  add_test(NAME test_rng COMMAND test_rng)
  # The compact, lazy block index (S1): birth ids, the text-source seam, the revive probe, the per-blob analysis. It uses the corpus's edit stream as its history.
  add_executable(test_block_index lib/tests/test_block_index.cpp)
  target_compile_options(test_block_index PRIVATE ${SUB0CANVAS_STRICT} -g)
  target_link_libraries(test_block_index PRIVATE sub0canvas sub0canvas_bench_corpus simdjson::simdjson Sub0Platform::sub0platform)
  add_test(NAME test_block_index COMMAND test_block_index)
  # The source-anchor resolver (B4): truth from the mock attempts (fixtures/mock) and the corpus's edit stream, so it links the corpus generator too.
  add_executable(test_resolver lib/tests/test_resolver.cpp)
  target_compile_options(test_resolver PRIVATE ${SUB0CANVAS_STRICT} -g)
  target_include_directories(test_resolver PRIVATE lib/tests)
  target_link_libraries(test_resolver PRIVATE sub0canvas sub0canvas_bench_corpus simdjson::simdjson Sub0Platform::sub0platform)
  add_test(NAME test_resolver COMMAND test_resolver ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
  # The determinism rules on the C++ text (tools/check_cxx_rules.cmake; AGENTS.md Code style): no <random> outside app/, no std::hash in lib/src, no new snprintf, one
  # generator draw per statement. A script run by cmake itself: no dependency, quick tier.
  add_test(NAME check_cxx_rules COMMAND ${CMAKE_COMMAND} -DREPO=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_cxx_rules.cmake)
  # The dependency-reference rule (tools/check_deps_policy.cmake; AGENTS.md "Dependencies"): no raw 40-hex GIT_TAG outside cmake/sub0_deps_pinned.cmake. Quick tier.
  add_test(NAME check_deps_policy COMMAND ${CMAKE_COMMAND} -DREPO=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_deps_policy.cmake)
  # The viewer's tests (test/*.test.js: the model and the demo fixtures) run under node when it is installed, so CI covers both halves.
  find_program(SUB0CANVAS_NODE node)
  if(SUB0CANVAS_NODE)
    file(GLOB viewerTests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test/*.test.js)
    add_test(NAME test_viewer COMMAND ${SUB0CANVAS_NODE} --test ${viewerTests} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    # The tests build temporary git repositories. A git hook (the pre-push gate) runs with GIT_DIR and friends set, which in a linked worktree point at the
    # worktree's own git directory and break every `git -C <temp>` below; the tests must not inherit them.
    set_tests_properties(test_viewer PROPERTIES ENVIRONMENT_MODIFICATION "GIT_DIR=unset:;GIT_WORK_TREE=unset:;GIT_INDEX_FILE=unset:;GIT_PREFIX=unset:;GIT_COMMON_DIR=unset:")
    if(SUB0CANVAS_TOP_LEVEL)
      set_tests_properties(test_viewer PROPERTIES ENVIRONMENT "SUB0CANVAS_EXE=$<TARGET_FILE:sub0canvas_app>")   # test/snapshot.test.js runs it
    endif()
    # K0, the viewer's interactivity timing (docs/K0.md): headless Edge or Chrome over DevTools, local only, label timing (set below, so quick and full exclude
    # it). It prints a line beginning "SKIP timing" and exits 0 on battery, on a busy machine or without a browser; ctest then reports it skipped.
    add_test(NAME Sub0Canvas_K0_timing COMMAND ${SUB0CANVAS_NODE} tools/k0/run.mjs WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(Sub0Canvas_K0_timing PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE SKIP_REGULAR_EXPRESSION "^SKIP timing")
  else()
    message(STATUS "Sub0Canvas: node not found; the viewer's tests (test_viewer) are not registered")
  endif()
  # Test tiers (CraigHutchinson/.github docs/ci-strategy.md): every test is fast, so all are smoke; `ctest --preset quick` excludes the labels
  # full and timing (the timing test is local only). A long suite added later is labelled full.
  get_property(sub0canvasTests DIRECTORY PROPERTY TESTS)
  list(REMOVE_ITEM sub0canvasTests Sub0Canvas_Bench_timing Sub0Canvas_K0_timing)   # their own label, timing, set below and above
  set_tests_properties(${sub0canvasTests} PROPERTIES LABELS smoke)
  if(TEST Sub0Canvas_K0_timing)
    set_tests_properties(Sub0Canvas_K0_timing PROPERTIES LABELS timing)
  endif()

  # The consumer rung (docs/MOCK_SQUAD.md, docs/TESTING.md): consumer/mock_squad is a separate CMake project that fetches this checkout through CPM, exactly as
  # Sub0Squad does, and links only the public target. `ctest --build-and-test` configures, builds and runs it; CPM_Sub0Canvas_SOURCE points it at this source tree,
  # so what it proves is this checkout as a consumer sees it. Labelled full: it builds the library a second time (see the measured cost in docs/MOCK_SQUAD.md).
  set(consumerOptions -DCPM_Sub0Canvas_SOURCE=${CMAKE_CURRENT_SOURCE_DIR} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCMAKE_BUILD_TYPE=Release)
  if(DEFINED Sub0Platform_SOURCE_DIR)
    list(APPEND consumerOptions -DCPM_Sub0Platform_SOURCE=${Sub0Platform_SOURCE_DIR})   # the ref this build already resolved
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
