#pragma once
/// @file content_id.hpp
/// The content id of a text: what a snapshot refers to instead of repeating the text (docs/BACKBONE.md section 7, `texts`; SCALE.md 6.1 measured 213 MB for 8
/// turns of 1,590 files, nearly all repeats). Pure; no I/O.

#include <string>
#include <string_view>

namespace sub0canvas {

/// The git blob id of `text`: the SHA-1 of `"blob <size>\0"` followed by the bytes, as 40 lower-case hex digits (what `git hash-object` prints). Chosen so an id
/// computed here equals the one git holds for the same bytes; it names content, never a place, and is stable across runs, platforms and compilers (`std::hash` is not).
[[nodiscard]] std::string contentId(std::string_view text);

}  // namespace sub0canvas
