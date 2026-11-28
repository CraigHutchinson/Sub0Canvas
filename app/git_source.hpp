#pragma once
// The git DocumentSource (docs/BACKBONE.md section 5, amendments A20 and A21): runs git, once per question, and hands its output to the library's pure parsers
// (sub0canvas/git_plumbing.hpp). It is the process runner the library may not be.
//
// How it keeps to A21: history is `git log --first-parent -- <scope>`, which uses the commit-graph and its changed-path Bloom filters when the repository has
// them (it never walks a revision that did not change the scope); objects are read through `git cat-file --batch`, many per process (popen is one-directional, so
// the requests go in through a temporary file: one process per batch rather than one long-lived one); trees come from `ls-tree` of the scope only, never the
// whole tree. Nothing is fetched: GIT_NO_LAZY_FETCH and GIT_TERMINAL_PROMPT keep a partial clone's missing objects missing, which the source reports as unknown (A20).
#include <sub0canvas/git_plumbing.hpp>
#include <sub0canvas/source.hpp>

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace sub0canvas::app {

class GitSource final : public DocumentSource
{
public:
    /// A local repository (a working tree or a bare one). Git child processes are configured to avoid lazy fetches and prompts.
    explicit GitSource(std::string repo);

    [[nodiscard]] History history(const std::string& to, const Scope& scope, std::size_t limit) const override;
    [[nodiscard]] std::optional<std::vector<PathChange>> changes(const std::string& parent, const std::string& revision) const override;
    [[nodiscard]] std::vector<FileText> read(const std::vector<FileRef>& refs) const override;
    [[nodiscard]] Listing list(const std::string& revision, const Scope& scope) const override;
    [[nodiscard]] std::optional<std::string> resolve(const std::string& name) const override;

    /// The subject line of a commit, for display (not part of the source seam: identity never depends on it).
    [[nodiscard]] std::optional<std::string> subject(const std::string& revision) const;

    /// The git commands run so far (the arguments after `-C <repo>`), for tracing and for tests that check what was asked.
    [[nodiscard]] const std::vector<std::string>& commands() const { return commands_; }

private:
    /// Runs `git -C repo --literal-pathspecs <args>` (stdin from `stdinFile` when given); the output, or nullopt when git exited non-zero.
    [[nodiscard]] std::optional<std::string> run(const std::string& args, const std::string& stdinFile = {}) const;
    [[nodiscard]] bool shallow() const;

    std::string repo_;
    mutable std::vector<std::string> commands_;
    mutable std::optional<bool> shallow_;
    mutable std::map<std::string, std::optional<std::string>> resolved_;
    mutable std::set<std::string> boundary_;   ///< the commits a shallow clone cut off (its `shallow` file)
};

/// Runs `git -C repo <args>` through the shell (stdin from `stdinFile` when given): the output, or nullopt when git could not run or exited non-zero.
/// The one process runner of the command line; `args` must already be shell-safe (`pathspecArgs`).
[[nodiscard]] std::optional<std::string> runGit(const std::string& repo, const std::string& args, const std::string& stdinFile = {});

/// The pathspec arguments of a scope: ` -- "a" "b"`; empty for the whole tree. nullopt when an entry holds a character the shell cannot be trusted with.
[[nodiscard]] std::optional<std::string> pathspecArgs(const Scope& scope);

}  // namespace sub0canvas::app
