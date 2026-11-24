cmake_minimum_required(VERSION 3.24)
# The mock squad (docs/MOCK_SQUAD.md): Sub0Canvas tested as a project the squad consumes. This is a separate CMake project that fetches the library exactly as
# Sub0Squad's cmake/dependencies.cmake does (CPMAddPackage NAME Sub0Canvas) and links only its public target, so it sees public headers and builds with its own
# -Werror. The top-level build runs it through `ctest --build-and-test` (test_consumer_mock_squad) with CPM_Sub0Canvas_SOURCE pointing at this checkout.
project(mock_squad LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MOCK_SQUAD_REPO ${CMAKE_CURRENT_LIST_DIR}/../..)
include(${MOCK_SQUAD_REPO}/cmake/get_cpm.cmake)
include(${MOCK_SQUAD_REPO}/cmake/print_link.cmake)   # std::println links on every toolchain (the mock squad tools print)

# Without the override (CPM_Sub0Canvas_SOURCE) this fetches the released tag: a deliberate pin, not the checkout.
CPMAddPackage(NAME Sub0Canvas GIT_REPOSITORY https://github.com/CraigHutchinson/Sub0Canvas.git GIT_TAG v0.4.0)
# The test helper, as the library's own tests use it. The top level passes CPM_Sub0Platform_SOURCE, so its pin is the one used there; this one serves a standalone run.
if(NOT TARGET Sub0Platform::sub0platform)
  CPMAddPackage(NAME Sub0Platform
    GIT_REPOSITORY https://github.com/CraigHutchinson/Sub0Platform.git
    GIT_TAG 5feccdf226046cc790214bc30793832a1888ce4b
    SOURCE_SUBDIR cpp)
endif()

# The consumer's own warnings policy, as the squad's (GCC style).
set(MOCK_SQUAD_STRICT -Wall -Wextra -Werror)

add_library(mock_squad_lib STATIC src/scenario.cpp src/gate.cpp src/loop.cpp src/record.cpp)
target_include_directories(mock_squad_lib PUBLIC include)
target_compile_options(mock_squad_lib PRIVATE ${MOCK_SQUAD_STRICT})
target_link_libraries(mock_squad_lib PUBLIC sub0canvas::sub0canvas)

add_executable(mock_squad tools/mock_squad.cpp)
target_compile_options(mock_squad PRIVATE ${MOCK_SQUAD_STRICT})
target_link_libraries(mock_squad PRIVATE mock_squad_lib)

# The release the library says it is, and the newest tag of this checkout (when git can tell): check (f) of the consumer contract.
find_package(Git QUIET)
set(MOCK_SQUAD_LATEST_TAG "")
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0 --match "v*" WORKING_DIRECTORY ${MOCK_SQUAD_REPO}
                  OUTPUT_VARIABLE MOCK_SQUAD_LATEST_TAG OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE tagResult)
  if(NOT tagResult EQUAL 0)
    set(MOCK_SQUAD_LATEST_TAG "")
  endif()
endif()

# project(sub0canvas VERSION ...) sets its version in the library's own directory scope: read it from there, as any consumer asking "which release is this" would.
get_directory_property(MOCK_SQUAD_LIBRARY_VERSION DIRECTORY ${Sub0Canvas_SOURCE_DIR} DEFINITION sub0canvas_VERSION)

enable_testing()
add_executable(test_consumer_contract tests/test_consumer_contract.cpp)
target_compile_options(test_consumer_contract PRIVATE ${MOCK_SQUAD_STRICT} -g)
target_compile_definitions(test_consumer_contract PRIVATE
  MOCK_SQUAD_WERROR=1                                          # the options above carry -Werror: check (a) compiles under them
  MOCK_SQUAD_LIBRARY_VERSION="${MOCK_SQUAD_LIBRARY_VERSION}"
  MOCK_SQUAD_LATEST_TAG="${MOCK_SQUAD_LATEST_TAG}")
target_link_libraries(test_consumer_contract PRIVATE mock_squad_lib Sub0Platform::sub0platform)
add_test(NAME test_consumer_contract
  COMMAND test_consumer_contract ${MOCK_SQUAD_REPO}/fixtures/scenarios/squad ${MOCK_SQUAD_REPO}/fixtures/mock ${MOCK_SQUAD_REPO}/docs/REQUIREMENTS.md ${MOCK_SQUAD_REPO}/package.json)
