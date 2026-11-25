#pragma once
/// @file record.hpp
/// The mock squad's record writer (docs/MOCK_SQUAD.md section 1): the persisted attempt in the squad's production format (`canvas.json`, `canvas/<revisionFileName>`,
/// `layers/turn-N.json`, `reply-N.txt`, `gate.txt`, `agent.json` as a subset) plus `mock-trace.json` (the outcome of each reply and the library calls made).
/// The loop fills `Attempt::record` as the squad's `persistCanvas` and verdicts would; `seal` adds the two summary files. `writeTree` and `readTree` move a
/// record to and from `fixtures/mock/<name>/`. Today's format only: B0 (MS1) changes it and regenerates the fixtures.

#include "mock_squad/loop.hpp"

#include <filesystem>
#include <map>
#include <string>

namespace mock_squad {

/// `s` as a JSON string literal, quotes included.
[[nodiscard]] std::string jsonString(const std::string& s);

/// Add `agent.json` and `mock-trace.json` to the attempt's record, from its outcomes and counts.
void seal(Attempt& attempt, const std::string& scenarioName);

/// Replace the directory `dir` with exactly these files.
void writeTree(const std::filesystem::path& dir, const std::map<std::string, std::string>& files);

/// Every file under `dir` (relative path with `/`, bytes as stored); empty when the directory is missing.
[[nodiscard]] std::map<std::string, std::string> readTree(const std::filesystem::path& dir);

}  // namespace mock_squad
