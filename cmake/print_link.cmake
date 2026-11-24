# std::println to a stream needs a library on some toolchains: MinGW's GCC 15+ keeps the terminal helpers (std::__open_terminal, std::__write_to_terminal) in libstdc++exp,
# while a Linux libstdc++ (gcc 14, clang 19) has them in libstdc++ itself. Probe once; link libstdc++exp for every target created after this include only when the plain
# link fails and the link with it works.
include_guard(GLOBAL)
include(CheckCXXSourceCompiles)
set(sub0canvasPrintProbe "#include <print>\nint main() { std::println(stderr, \"probe {}\", 1); return 0; }\n")
check_cxx_source_compiles("${sub0canvasPrintProbe}" SUB0CANVAS_PRINT_LINKS)
if(NOT SUB0CANVAS_PRINT_LINKS)
  set(CMAKE_REQUIRED_LIBRARIES stdc++exp)
  check_cxx_source_compiles("${sub0canvasPrintProbe}" SUB0CANVAS_PRINT_LINKS_WITH_EXP)
  unset(CMAKE_REQUIRED_LIBRARIES)
  if(SUB0CANVAS_PRINT_LINKS_WITH_EXP)
    link_libraries(stdc++exp)
  endif()
endif()
