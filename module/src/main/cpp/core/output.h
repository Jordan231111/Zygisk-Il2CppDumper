#pragma once
#include <cstdio>
#include <string>
#include <string_view>
namespace dumper {
class AtomicOutput {
  public:
    explicit AtomicOutput(const std::string& directory, std::string filename = "dump.cs");
    ~AtomicOutput();
    AtomicOutput(const AtomicOutput&) = delete;
    AtomicOutput& operator=(const AtomicOutput&) = delete;
    bool write(std::string_view data);
    bool commit();
    bool good() const { return stream_ && error_.empty(); }
    const std::string& error() const { return error_; }
    size_t bytes() const { return bytes_; }

  private:
    void fail(std::string_view operation);
    int directory_{-1};
    FILE* stream_{};
    std::string filename_, temporary_, error_;
    size_t bytes_{};
};
} // namespace dumper
