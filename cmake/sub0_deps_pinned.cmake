# The one table of the family's dependency refs (AGENTS.md "Dependencies"). Included by CMakeLists.txt and consumer/mock_squad/CMakeLists.txt.
# A tag where the repository has one; a raw 40-hex commit SHA only here (tools/check_deps_policy.cmake), with a comment naming the commit and its date.
# Third-party dependencies (simdjson) use a readable tag inline instead. To build against a local checkout instead of a pin, set CPM_<Name>_SOURCE (the override beats the pin).
set(SUB0_PIN_Sub0Platform "v0.2.0")                                    # tag v0.2.0 (b6579c8, 2026-11-24)
set(SUB0_PIN_Sub0Perf "7f69fabd880278fac6ba9e4915cff497855a5f17")      # no tag yet: the commit that renamed the names to sub0perf_* / SUB0PERF_* (2026-11-24)
set(SUB0_PIN_Sub0Canvas "v0.4.0")                                      # tag v0.4.0: the release the consumer rung fetches when it is not given a source
