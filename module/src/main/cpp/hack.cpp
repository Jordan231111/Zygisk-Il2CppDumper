#include "hack.h"
#include "core/maps.h"
#include "discovery.h"
#include "log.h"
#include "xdl.h"

#include <android/api-level.h>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/system_properties.h>
#include <thread>
#include <unistd.h>

namespace dumper {
namespace {
using Clock = std::chrono::steady_clock;
constexpr uint32_t kBridgeMagic = 0x494c3244;
struct BridgeRequest {
    uint32_t magic;
    const char* directory;
    const char* process;
    uint32_t timeout;
    uint32_t verbose;
};

const char* architecture() {
#if defined(__aarch64__)
    return "arm64-v8a";
#elif defined(__arm__)
    return "armeabi-v7a";
#elif defined(__x86_64__)
    return "x86_64";
#else
    return "x86";
#endif
}

void run_native(const WorkerContext& context) {
    const auto deadline = Clock::now() + std::chrono::seconds(context.options.timeout_seconds);
    LOGI("stage=discover library=libil2cpp.so timeout_seconds=%u", context.options.timeout_seconds);
    auto next_fallback = Clock::now();
    do {
        const bool fallback = Clock::now() >= next_fallback;
        if (fallback)
            next_fallback = Clock::now() + std::chrono::seconds(1);
        if (void* handle = discover_il2cpp(fallback)) {
            struct Handle {
                void* value;
                ~Handle() { xdl_close(value); }
            } owner{handle};
            dump_runtime(handle, context.data_directory, context.options);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } while (Clock::now() < deadline);
    LOGE("stage=discover fatal=library-timeout abi=%s", architecture());
}

#if defined(__i386__) || defined(__x86_64__)
// Stable prefix of AOSP NativeBridgeCallbacks. Fields after version 2 are read
// only when the bridge advertises version >= 3. No namespace address is guessed.
struct NativeBridgeCallbacks {
    uint32_t version;
    void* initialize;
    void* (*loadLibrary)(const char*, int);
    void* (*getTrampoline)(void*, const char*, const char*, uint32_t);
    void *isSupported, *getAppEnv, *isCompatibleWith, *getSignalHandler;
    void *unloadLibrary, *getError, *isPathSupported, *initAnonymousNamespace;
    void *createNamespace, *linkNamespaces;
    void* (*loadLibraryExt)(const char*, int, void*);
    void* getVendorNamespace;
    void* getExportedNamespace;
    void* preZygoteFork;
    void* (*getTrampolineWithJNICallType)(void*, const char*, const char*, uint32_t, int);
};

std::string native_library_directory(JavaVM* vm) {
    if (!vm)
        return {};
    JNIEnv* env = nullptr;
    const auto status = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    const bool attached = status == JNI_EDETACHED;
    if ((attached && vm->AttachCurrentThread(&env, nullptr) != JNI_OK) ||
        (!attached && status != JNI_OK))
        return {};
    struct Scope {
        JavaVM* vm;
        JNIEnv* env;
        bool attached;
        bool frame{};
        ~Scope() {
            if (env->ExceptionCheck())
                env->ExceptionClear();
            if (frame)
                env->PopLocalFrame(nullptr);
            if (attached)
                vm->DetachCurrentThread();
        }
    } scope{vm, env, attached};
    if (env->PushLocalFrame(16) != JNI_OK)
        return {};
    scope.frame = true;
    auto activity_thread = env->FindClass("android/app/ActivityThread");
    if (!activity_thread || env->ExceptionCheck())
        return {};
    auto current = env->GetStaticMethodID(activity_thread, "currentApplication",
                                          "()Landroid/app/Application;");
    if (!current || env->ExceptionCheck())
        return {};
    auto application = env->CallStaticObjectMethod(activity_thread, current);
    if (!application || env->ExceptionCheck())
        return {};
    auto app_class = env->GetObjectClass(application);
    if (!app_class || env->ExceptionCheck())
        return {};
    auto get_info =
        env->GetMethodID(app_class, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;");
    if (!get_info || env->ExceptionCheck())
        return {};
    auto info = env->CallObjectMethod(application, get_info);
    if (!info || env->ExceptionCheck())
        return {};
    auto info_class = env->GetObjectClass(info);
    if (!info_class || env->ExceptionCheck())
        return {};
    auto field = env->GetFieldID(info_class, "nativeLibraryDir", "Ljava/lang/String;");
    if (!field || env->ExceptionCheck())
        return {};
    auto directory = static_cast<jstring>(env->GetObjectField(info, field));
    if (!directory || env->ExceptionCheck())
        return {};
    const char* raw = env->GetStringUTFChars(directory, nullptr);
    if (!raw)
        return {};
    std::string result(raw);
    env->ReleaseStringUTFChars(directory, raw);
    return result;
}

bool run_bridge(const WorkerContext& context) {
    if (!context.vm || context.bridge_payload.empty())
        return false;
    std::array<char, PROP_VALUE_MAX> bridge{};
    if (__system_property_get("ro.dalvik.vm.native.bridge", bridge.data()) <= 0 ||
        std::strcmp(bridge.data(), "0") == 0)
        return false;
    std::string directory;
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    do {
        directory = native_library_directory(context.vm);
        if (!directory.empty())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } while (Clock::now() < deadline);
    if (directory.find("/lib/x86") != std::string::npos)
        return false;
    if (directory.empty()) {
        LOGW("stage=native-bridge reason=application-info-unavailable");
        return false;
    }
    auto* handle = xdl_open(bridge.data(), XDL_DEFAULT);
    if (!handle) {
        LOGW("stage=native-bridge reason=bridge-not-loaded");
        return false;
    }
    auto* table = xdl_sym(handle, "NativeBridgeItf", nullptr);
    Memory memory;
    const auto version = memory.read<uint32_t>(reinterpret_cast<uintptr_t>(table));
    NativeBridgeCallbacks callbacks{};
    const size_t bytes = version && *version >= 7 ? sizeof(callbacks)
                         : version && *version >= 3
                             ? offsetof(NativeBridgeCallbacks, getVendorNamespace)
                             : offsetof(NativeBridgeCallbacks, unloadLibrary);
    const bool copied = version && *version >= 2 &&
                        memory.read(reinterpret_cast<uintptr_t>(table), &callbacks, bytes);
    xdl_close(handle);
    const auto maps = Maps::read_self();
    const auto callable = [&](auto function) {
        return function && maps.executable(reinterpret_cast<uintptr_t>(function));
    };
    const bool old_load = copied && callable(callbacks.loadLibrary);
    const bool new_load = copied && callbacks.version >= 3 && callable(callbacks.loadLibraryExt);
    const bool new_trampoline =
        copied && callbacks.version >= 7 && callable(callbacks.getTrampolineWithJNICallType);
    if ((!old_load && !new_load) || (!new_trampoline && !callable(callbacks.getTrampoline))) {
        LOGW("stage=native-bridge reason=unsupported-callbacks");
        return false;
    }
    const int fd = static_cast<int>(syscall(SYS_memfd_create, "il2cppdumper-bridge", MFD_CLOEXEC));
    if (fd < 0) {
        LOGW("stage=native-bridge reason=memfd-failed errno=%d", errno);
        return false;
    }
    struct File {
        int fd;
        ~File() { close(fd); }
    } file{fd};
    size_t done = 0;
    while (done < context.bridge_payload.size()) {
        const auto count =
            write(fd, context.bridge_payload.data() + done, context.bridge_payload.size() - done);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            LOGW("stage=native-bridge reason=payload-write-failed errno=%d", errno);
            return false;
        }
        done += static_cast<size_t>(count);
    }
    const auto path = "/proc/self/fd/" + std::to_string(fd);
    void* arm = old_load ? callbacks.loadLibrary(path.c_str(), RTLD_NOW) : nullptr;
    if (!arm && new_load)
        arm = callbacks.loadLibraryExt(path.c_str(), RTLD_NOW, nullptr);
    if (!arm) {
        LOGW("stage=native-bridge reason=load-failed version=%u", callbacks.version);
        return false;
    }
    constexpr int kRegularJniCall = 1;
    auto trampoline =
        new_trampoline
            ? callbacks.getTrampolineWithJNICallType(arm, "JNI_OnLoad", nullptr, 0, kRegularJniCall)
            : callbacks.getTrampoline(arm, "JNI_OnLoad", nullptr, 0);
    auto init = reinterpret_cast<jint (*)(JavaVM*, void*)>(trampoline);
    if (!init) {
        LOGW("stage=native-bridge reason=trampoline-unavailable");
        return false;
    }
    BridgeRequest request{kBridgeMagic, context.data_directory.c_str(),
                          context.options.process.c_str(), context.options.timeout_seconds,
                          context.options.verbose};
    if (init(context.vm, &request) != JNI_VERSION_1_6) {
        LOGW("stage=native-bridge reason=JNI-OnLoad-failed");
        return false;
    }
    LOGI("stage=native-bridge loaded version=%u", callbacks.version);
    return true;
}
#endif

void* worker(void* parameter) {
    std::unique_ptr<WorkerContext> context(static_cast<WorkerContext*>(parameter));
    LOGI("stage=worker pid=%d tid=%d abi=%s api=%d page_size=%ld", getpid(), gettid(),
         architecture(), android_get_device_api_level(), sysconf(_SC_PAGESIZE));
#if defined(__i386__) || defined(__x86_64__)
    if (run_bridge(*context))
        return nullptr;
#endif
    run_native(*context);
    return nullptr;
}
} // namespace

bool start_worker(std::unique_ptr<WorkerContext> context) {
    if (!context)
        return false;
    pthread_attr_t attributes;
    int result = pthread_attr_init(&attributes);
    if (result != 0) {
        LOGE("pthread_attr_init: %s", std::strerror(result));
        return false;
    }
    result = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    pthread_t thread{};
    if (result == 0)
        result = pthread_create(&thread, &attributes, worker, context.get());
    pthread_attr_destroy(&attributes);
    if (result != 0) {
        LOGE("pthread_create: %s", std::strerror(result));
        return false;
    }
    context.release();
    return true;
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    if (!reserved)
        return JNI_VERSION_1_6;
    Memory memory;
    const auto request = memory.read<BridgeRequest>(reinterpret_cast<uintptr_t>(reserved));
    if (!request || request->magic != kBridgeMagic || !request->directory || !request->process ||
        request->timeout == 0 || request->timeout > 600 || request->verbose > 1)
        return JNI_ERR;
    auto maps = Maps::read_self();
    const auto directory =
        memory.string(reinterpret_cast<uintptr_t>(request->directory), maps, 4096);
    const auto process = memory.string(reinterpret_cast<uintptr_t>(request->process), maps, 256);
    if (!directory || directory->empty() || directory->front() != '/' || !process)
        return JNI_ERR;
    auto context = std::make_unique<WorkerContext>();
    context->vm = vm;
    context->data_directory = *directory;
    context->options.process = *process;
    context->options.timeout_seconds = request->timeout;
    context->options.verbose = request->verbose != 0;
    return start_worker(std::move(context)) ? JNI_VERSION_1_6 : JNI_ERR;
}
} // namespace dumper
