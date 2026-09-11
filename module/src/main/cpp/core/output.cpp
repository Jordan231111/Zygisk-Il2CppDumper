#include "output.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace dumper {
void AtomicOutput::fail(std::string_view operation) {
    if (error_.empty())
        error_ = std::string(operation) + ": " + std::strerror(errno);
}
AtomicOutput::AtomicOutput(const std::string& directory, std::string filename)
    : filename_(std::move(filename)) {
    if (filename_.empty() || filename_.find('/') != std::string::npos || filename_ == "." ||
        filename_ == "..") {
        error_ = "invalid output filename";
        return;
    }
    directory_ = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory_ < 0) {
        fail("open output directory");
        return;
    }
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        temporary_ = "." + filename_ + "." + std::to_string(getpid()) + "." +
                     std::to_string(attempt) + ".tmp";
        const int fd = openat(directory_, temporary_.c_str(),
                              O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd >= 0) {
            stream_ = fdopen(fd, "wb");
            if (!stream_) {
                fail("fdopen output");
                close(fd);
                unlinkat(directory_, temporary_.c_str(), 0);
                temporary_.clear();
            }
            return;
        }
        if (errno != EEXIST) {
            temporary_.clear();
            fail("create output");
            return;
        }
    }
    temporary_.clear(); // None of the existing files belong to this writer.
    fail("create unique output");
}
AtomicOutput::~AtomicOutput() {
    if (stream_)
        fclose(stream_);
    if (!temporary_.empty() && directory_ >= 0)
        unlinkat(directory_, temporary_.c_str(), 0);
    if (directory_ >= 0)
        close(directory_);
}
bool AtomicOutput::write(std::string_view data) {
    if (!good())
        return false;
    if (fwrite(data.data(), 1, data.size(), stream_) != data.size()) {
        fail("write output");
        return false;
    }
    bytes_ += data.size();
    return true;
}
bool AtomicOutput::commit() {
    if (!good())
        return false;
    if (fflush(stream_) != 0 || fsync(fileno(stream_)) != 0) {
        fail("flush output");
        return false;
    }
    const int result = fclose(stream_);
    stream_ = nullptr;
    if (result != 0) {
        fail("close output");
        return false;
    }
    if (renameat(directory_, temporary_.c_str(), directory_, filename_.c_str()) != 0) {
        fail("publish output");
        return false;
    }
    temporary_.clear();
    return true;
}
} // namespace dumper
