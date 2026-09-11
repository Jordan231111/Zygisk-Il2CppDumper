#include "core/target.h"
#include "game.h"
#include "hack.h"
#include "log.h"
#include "zygisk.hpp"

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
struct Fd {
    int value{-1};
    ~Fd() {
        if (value >= 0)
            close(value);
    }
};
class UtfChars {
  public:
    UtfChars(JNIEnv* env, jstring text)
        : env_(env), text_(text), chars_(text ? env->GetStringUTFChars(text, nullptr) : nullptr) {}
    ~UtfChars() {
        if (chars_)
            env_->ReleaseStringUTFChars(text_, chars_);
    }
    const char* get() const { return chars_; }

  private:
    JNIEnv* env_;
    jstring text_;
    const char* chars_;
};
} // namespace

class DumperModule final : public zygisk::ModuleBase {
  public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        api_ = api;
        env_ = env;
    }
    void preServerSpecialize(zygisk::ServerSpecializeArgs*) override { unload(); }
    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        context_.reset();
        if (!args || !args->nice_name) {
            unload();
            return;
        }
        UtfChars process(env_, args->nice_name);
        if (!process.get()) {
            clear_jni_error();
            unload();
            return;
        }
        Fd module_directory{api_->getModuleDir()};
        std::string targets = GamePackageName;
        bool verbose = false;
        if (module_directory.value >= 0) {
            Fd config{
                openat(module_directory.value, "targets.txt", O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
            if (config.value >= 0) {
                std::array<char, 4097> buffer{};
                ssize_t count;
                do {
                    count = read(config.value, buffer.data(), buffer.size());
                } while (count < 0 && errno == EINTR);
                if (count < 0 || count > 4096) {
                    unload();
                    return;
                }
                targets.assign(buffer.data(), static_cast<size_t>(count));
            } else if (errno != ENOENT) {
                unload();
                return;
            }
            verbose = faccessat(module_directory.value, "verbose", F_OK, 0) == 0;
        }
        if (!dumper::matches_target(process.get(), targets)) {
            unload();
            return;
        }
        if (!args->app_data_dir) {
            LOGE("stage=target fatal=null-app-data-directory process=%s", process.get());
            unload();
            return;
        }
        UtfChars directory(env_, args->app_data_dir);
        if (!directory.get()) {
            clear_jni_error();
            unload();
            return;
        }
        context_ = std::make_unique<dumper::WorkerContext>();
        context_->data_directory = directory.get();
        context_->options.process = process.get();
        context_->options.verbose = verbose;
        if (env_->GetJavaVM(&context_->vm) != JNI_OK)
            context_->vm = nullptr;
        LOGI("stage=target process=%s directory=%s", process.get(), directory.get());
#if defined(__i386__) || defined(__x86_64__)
#if defined(__i386__)
        const char* payload = "zygisk/armeabi-v7a.so";
#else
        const char* payload = "zygisk/arm64-v8a.so";
#endif
        if (module_directory.value >= 0) {
            Fd file{openat(module_directory.value, payload, O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
            struct stat status{};
            if (file.value >= 0 && fstat(file.value, &status) == 0 && S_ISREG(status.st_mode) &&
                status.st_size > 0 && status.st_size <= 16 * 1024 * 1024) {
                context_->bridge_payload.resize(static_cast<size_t>(status.st_size));
                size_t done = 0;
                while (done < context_->bridge_payload.size()) {
                    const auto count = read(file.value, context_->bridge_payload.data() + done,
                                            context_->bridge_payload.size() - done);
                    if (count < 0 && errno == EINTR)
                        continue;
                    if (count <= 0) {
                        context_->bridge_payload.clear();
                        break;
                    }
                    done += static_cast<size_t>(count);
                }
            }
        }
#endif
    }
    void postAppSpecialize(const zygisk::AppSpecializeArgs*) override {
        if (context_ && !dumper::start_worker(std::move(context_)))
            LOGE("stage=worker fatal=thread-create-failed");
    }

  private:
    void unload() { api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY); }
    void clear_jni_error() {
        if (env_->ExceptionCheck())
            env_->ExceptionClear();
    }
    zygisk::Api* api_{};
    JNIEnv* env_{};
    std::unique_ptr<dumper::WorkerContext> context_;
};
REGISTER_ZYGISK_MODULE(DumperModule)
