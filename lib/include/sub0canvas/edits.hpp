#pragma once
/// @file edits.hpp
/// Edit-style replies: instead of writing a whole file again to change a few lines, a worker replies with search and replace blocks (docs/PROMPTS.md:
/// only about 19 % of the lines of repair replies had changed). Whole files stay the fallback.

#include <expected>
#include <map>
#include <set>
#include <string>

namespace sub0canvas {

/// Apply every `=== EDIT: path ===` block of `reply` (each holding `<<<<<<< SEARCH` / `=======` / `>>>>>>> REPLACE` blocks) to `files`. A SEARCH must match
/// the current text exactly once. Returns the complete updated set of files, or the reason, naming the edit that did not apply.
[[nodiscard]] std::expected<std::map<std::string, std::string>, std::string> applyEdits(const std::string& reply, std::map<std::string, std::string> files,
                                                                                         const std::set<std::string>& allowed);

/// The new text of a LINES, AFTER or SYMBOL edit whose body a worker wrapped in search/replace markers (`old lines` / `=======` / `new lines` /
/// `>>>>>>> REPLACE`): the part between the separator and the closing marker. A lone closing marker on the last nonblank line is removed.
/// Other bodies come back unchanged.
[[nodiscard]] std::string replacementOfMarkedBlock(const std::string& body);

}  // namespace sub0canvas
