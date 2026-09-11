#pragma once
#include <string_view>
namespace dumper {
bool valid_package(std::string_view value);
// Entries are newline-separated exact package/process names. A trailing :*
// explicitly opts into secondary processes of that package.
bool matches_target(std::string_view process, std::string_view targets);
} // namespace dumper
