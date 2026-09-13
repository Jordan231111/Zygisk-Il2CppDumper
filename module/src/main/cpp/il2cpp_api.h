#pragma once
#include "core/elf.h"
#include "il2cpp-class.h"
#include <cstddef>
#include <string>
namespace dumper {
struct Il2CppApi {
    Il2CppApi() = default;
    ~Il2CppApi();
    Il2CppApi(const Il2CppApi&) = delete;
    Il2CppApi& operator=(const Il2CppApi&) = delete;
#define API(result, name, parameters, required) result(*name) parameters{};
#include "il2cpp-api-functions.h"
#undef API
    bool load(void* handle, std::string& error);
    bool can_enumerate() const;
    bool can_calibrate_methods() const;
    elf::Image module_image;

  private:
    void* native_handle_{};
};
} // namespace dumper
