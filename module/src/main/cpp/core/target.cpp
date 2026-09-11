#include "target.h"
namespace dumper {
namespace {
bool letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
} // namespace
bool valid_package(std::string_view value) {
    bool start = true, dotted = false;
    for (char c : value) {
        if (c == '.') {
            if (start)
                return false;
            start = true;
            dotted = true;
        } else {
            if (!(letter(c) || (!start && digit(c))))
                return false;
            start = false;
        }
    }
    return dotted && !start;
}
bool matches_target(std::string_view process, std::string_view targets) {
    if (process.empty() || targets.size() > 4096)
        return false;
    while (!targets.empty()) {
        const auto end = targets.find('\n');
        auto entry = targets.substr(0, end);
        if (!entry.empty() && entry.back() == '\r')
            entry.remove_suffix(1);
        const auto colon = entry.find(':');
        const auto package = entry.substr(0, colon);
        if (valid_package(package)) {
            if (entry == process)
                return true;
            if (colon != std::string_view::npos && entry.substr(colon) == ":*" &&
                (process == package ||
                 (process.size() > package.size() + 1 && process.starts_with(package) &&
                  process[package.size()] == ':')))
                return true;
        }
        if (end == std::string_view::npos)
            break;
        targets.remove_prefix(end + 1);
    }
    return false;
}
} // namespace dumper
