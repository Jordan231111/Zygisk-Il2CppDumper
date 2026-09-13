#include "il2cpp_dump.h"
#include "core/binary.h"
#include "core/maps.h"
#include "core/output.h"
#include "core/runtime_layout.h"
#include "il2cpp-tabledefs.h"
#include "il2cpp_api.h"
#include "log.h"
#include "xdl.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unordered_map>
#include <vector>

namespace dumper {
namespace {
constexpr size_t kMaxAssemblies = 65536;
constexpr size_t kMaxClassesPerImage = 1000000;
constexpr size_t kMaxMembersPerClass = 65536;
constexpr size_t kMaxParameters = 4096;
constexpr std::streamoff kMaxClassBytes = 16 * 1024 * 1024;
constexpr size_t kMaxOutputBytes = 1024ULL * 1024 * 1024;
constexpr size_t kMaxNameBytes = 65536;
constexpr size_t kMaxNameCacheBytes = 8 * 1024 * 1024;
constexpr size_t kMaxTypeCacheBytes = 32 * 1024 * 1024;
constexpr auto kDumpTimeout = std::chrono::minutes(5);
using Clock = std::chrono::steady_clock;

bool snapshot_assemblies(Il2CppApi& api, const Il2CppDomain* domain, const Memory& memory,
                         std::vector<const Il2CppAssembly*>& snapshot, std::string& error) {
    // Assembly loading can reallocate the runtime's borrowed array. Copy through
    // the kernel, then verify its address, count and contents before using it.
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        size_t count{};
        const auto** source = api.domain_get_assemblies(domain, &count);
        if (!source || count == 0 || count > kMaxAssemblies) {
            error = "invalid-assembly-count count=" + std::to_string(count);
            return false;
        }
        snapshot.resize(count);
        if (memory.read(reinterpret_cast<uintptr_t>(source), snapshot.data(),
                        count * sizeof(void*))) {
            size_t current_count{};
            const auto** current = api.domain_get_assemblies(domain, &current_count);
            if (current == source && current_count == count) {
                std::vector<const Il2CppAssembly*> verification(count);
                if (memory.read(reinterpret_cast<uintptr_t>(current), verification.data(),
                                count * sizeof(void*)) &&
                    verification == snapshot)
                    return true;
            }
        }
        if (attempt < 2)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    error = "assembly-array-unreadable-or-changing";
    return false;
}

std::string access_modifier(uint32_t flags, bool method) {
    const auto access = flags & 7U; // ECMA-335 member access bits, identical for fields/methods.
    switch (access) {
    case 1:
        return "private ";
    case 2:
        return "private protected ";
    case 3:
        return "internal ";
    case 4:
        return "protected ";
    case 5:
        return "protected internal ";
    case 6:
        return "public ";
    default:
        return method ? "private " : "";
    }
}
std::string method_modifier(uint32_t flags) {
    std::string result = access_modifier(flags, true);
    if (flags & METHOD_ATTRIBUTE_STATIC)
        result += "static ";
    if (flags & METHOD_ATTRIBUTE_ABSTRACT) {
        result += "abstract ";
        if (!(flags & METHOD_ATTRIBUTE_NEW_SLOT))
            result += "override ";
    } else if (flags & METHOD_ATTRIBUTE_FINAL) {
        if (flags & METHOD_ATTRIBUTE_VIRTUAL && !(flags & METHOD_ATTRIBUTE_NEW_SLOT))
            result += "sealed override ";
    } else if (flags & METHOD_ATTRIBUTE_VIRTUAL) {
        result += (flags & METHOD_ATTRIBUTE_NEW_SLOT) ? "virtual " : "override ";
    }
    if (flags & METHOD_ATTRIBUTE_PINVOKE_IMPL)
        result += "extern ";
    return result;
}

class Writer {
  public:
    Writer(Il2CppApi& api, AtomicOutput& output, uintptr_t base)
        : api_(api), output_(output), base_(base), maps_(Maps::read_self()) {}

    std::string name(const char* pointer) {
        if (!pointer)
            return "<unknown>";
        if (const auto found = names_.find(pointer); found != names_.end())
            return found->second;
        auto result = memory_.string(reinterpret_cast<uintptr_t>(pointer), maps_, kMaxNameBytes);
        if (!result) {
            if (++invalid_names <= 3)
                LOGW("stage=format invalid_name=%p", static_cast<const void*>(pointer));
            return "<invalid-name>";
        }
        for (char& character : *result) {
            const auto byte = static_cast<unsigned char>(character);
            if (byte < 32 || byte == 127)
                character = '?';
        }
        if (names_.size() < 65536 && result->size() <= kMaxNameCacheBytes - name_cache_bytes_) {
            name_cache_bytes_ += result->size();
            names_.emplace(pointer, *result);
        }
        return *result;
    }

    std::string_view type_name(const Il2CppType* type) {
        if (!type)
            return "<unknown>";
        if (auto it = type_names_.find(type); it != type_names_.end())
            return it->second;
        std::string result;
        if (auto* klass = api_.class_from_type(type)) {
            // Metadata v39 can supply compact type handles to member accessors.
            // Class::FromType normalizes those handles; the raw Type::GetName
            // export can misinterpret them as a full private Il2CppType.
            result = name(api_.class_get_name(klass));
            const auto space = name(api_.class_get_namespace(klass));
            if (!space.empty() && space != "<unknown>")
                result = space + "." + result;
        } else
            result = "<unknown>";
        // Byref is written as ref/out/in by the member formatter.
        if (!result.empty() && result.back() == '&')
            result.pop_back();
        if (type_names_.size() >= 1000000 ||
            result.size() > kMaxTypeCacheBytes - type_cache_bytes_) {
            error = "runtime type cache limit exceeded";
            return "<type-limit>";
        }
        type_cache_bytes_ += result.size();
        return type_names_.emplace(type, std::move(result)).first->second;
    }

    bool byref(const Il2CppType* type) {
        if (!type)
            return false;
        if (api_.type_is_byref)
            return api_.type_is_byref(type);
        if (api_.type_get_object && api_.object_get_class && api_.class_get_method_from_name &&
            api_.runtime_invoke && api_.object_unbox) {
            auto* reflected = api_.type_get_object(type);
            if (!reflected)
                return false;
            auto* klass = api_.object_get_class(reflected);
            if (!klass)
                return false;
            const auto* getter = api_.class_get_method_from_name(klass, "get_IsByRef", 0);
            auto* boxed = invoke(getter, reflected, nullptr, "Type.IsByRef");
            if (!boxed)
                return false;
            const auto result =
                memory_.read<uint8_t>(reinterpret_cast<uintptr_t>(api_.object_unbox(boxed)));
            return result && *result == 1;
        }
        return false;
    }

    Il2CppObject* invoke(const MethodInfo* method, void* instance, void** args, const char* stage) {
        if (!method || !api_.runtime_invoke) {
            error = std::string("missing reflection method: ") + stage;
            return nullptr;
        }
        Il2CppException* exception = nullptr;
        const auto* target = method;
        if (instance && api_.object_get_virtual_method) {
            if (const auto* resolved =
                    api_.object_get_virtual_method(static_cast<Il2CppObject*>(instance), method))
                target = resolved;
        }
        auto* value = api_.runtime_invoke(target, instance, args, &exception);
        if (exception) {
            error = std::string("managed exception in ") + stage;
            return nullptr;
        }
        return value;
    }

    void calibrate_method_layout() {
        const auto unavailable = [](const char* reason) {
            LOGW("method_address_strategy=unavailable reason=%s", reason);
        };
        if (!api_.can_calibrate_methods()) {
            unavailable("calibration-APIs-absent");
            return;
        }
        auto* module = api_.class_from_name(api_.get_corlib(), "System.Reflection", "Module");
        if (!module) {
            unavailable("System.Reflection.Module-absent");
            return;
        }
        api_.runtime_class_init(module);
        std::optional<size_t> candidate;
        for (const char* field_name : {"FilterTypeName", "FilterTypeNameIgnoreCase"}) {
            auto* field = api_.class_get_field_from_name(module, field_name);
            if (!field || !(api_.field_get_flags(field) & FIELD_ATTRIBUTE_STATIC)) {
                unavailable("static-filter-field-absent");
                return;
            }
            const auto* field_type = api_.field_get_type(field);
            auto* field_class = field_type ? api_.class_from_type(field_type) : nullptr;
            if (!field_class || api_.class_is_valuetype(field_class)) {
                unavailable("filter-field-is-not-reference-type");
                return;
            }
            Il2CppObject* delegate = nullptr;
            api_.field_static_get_value(field, &delegate);
            if (!delegate) {
                unavailable("null-filter-delegate");
                return;
            }
            auto* klass = api_.object_get_class(delegate);
            if (!klass) {
                unavailable("delegate-class-absent");
                return;
            }
            auto* pointer_field = api_.class_get_field_from_name(klass, "method_ptr");
            auto* method_field = api_.class_get_field_from_name(klass, "method");
            if (!pointer_field || !method_field) {
                unavailable("delegate-fields-absent");
                return;
            }
            for (auto* item : {pointer_field, method_field}) {
                const auto field_name_type = type_name(api_.field_get_type(item));
                if (field_name_type != "System.IntPtr" && field_name_type != "System.UIntPtr") {
                    unavailable("delegate-field-is-not-pointer-sized");
                    return;
                }
            }
            uintptr_t pointer{}, method{};
            api_.field_get_value(delegate, pointer_field, &pointer);
            api_.field_get_value(delegate, method_field, &method);
            if (!pointer || !method || !maps_.executable(pointer)) {
                unavailable("invalid-delegate-address");
                return;
            }
            std::optional<size_t> found;
            for (size_t offset = 0; offset < 8 * sizeof(uintptr_t); offset += sizeof(uintptr_t)) {
                const auto address = checked_add(untag_address(method), offset);
                if (!address) {
                    unavailable("method-address-overflow");
                    return;
                }
                const auto value = memory_.read<uintptr_t>(*address);
                if (value && *value == pointer) {
                    found = offset;
                    break;
                }
            }
            if (!found || (candidate && candidate != found)) {
                unavailable("unrecognized-or-inconsistent-method-layout");
                return;
            }
            candidate = found;
        }
        method_offset_ = candidate;
        LOGI("method_address_strategy=delegate-calibration offset=%zu", *method_offset_);
    }

    bool write_type(Il2CppClass* klass, const std::string& image) {
        if (!klass) {
            error = "null class returned by image enumeration";
            return false;
        }
        if (Clock::now() > deadline_) {
            error = "dump deadline exceeded";
            return false;
        }
        std::ostringstream out;
        out << "\n// Dll : " << image << "\n// Namespace: " << name(api_.class_get_namespace(klass))
            << '\n';
        const auto flags = static_cast<uint32_t>(api_.class_get_flags(klass));
        const bool value_type = api_.class_is_valuetype(klass), is_enum = api_.class_is_enum(klass);
        if (flags & TYPE_ATTRIBUTE_SERIALIZABLE)
            out << "[Serializable]\n";
        switch (flags & TYPE_ATTRIBUTE_VISIBILITY_MASK) {
        case TYPE_ATTRIBUTE_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_PUBLIC:
            out << "public ";
            break;
        case TYPE_ATTRIBUTE_NESTED_PRIVATE:
            out << "private ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAMILY:
            out << "protected ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAM_AND_ASSEM:
            out << "private protected ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAM_OR_ASSEM:
            out << "protected internal ";
            break;
        default:
            out << "internal ";
        }
        if ((flags & TYPE_ATTRIBUTE_ABSTRACT) && (flags & TYPE_ATTRIBUTE_SEALED))
            out << "static ";
        else if (!(flags & TYPE_ATTRIBUTE_INTERFACE) && (flags & TYPE_ATTRIBUTE_ABSTRACT))
            out << "abstract ";
        else if (!value_type && !is_enum && (flags & TYPE_ATTRIBUTE_SEALED))
            out << "sealed ";
        out << ((flags & TYPE_ATTRIBUTE_INTERFACE) ? "interface "
                : is_enum                          ? "enum "
                : value_type                       ? "struct "
                                                   : "class ");
        out << name(api_.class_get_name(klass));
        bool parent_written = false;
        auto* parent = api_.class_get_parent(klass);
        if (!value_type && !is_enum && parent) {
            const auto parent_name = type_name(api_.class_get_type(parent));
            if (parent_name != "System.Object" && parent_name != "Object") {
                out << " : " << parent_name;
                parent_written = true;
            }
        }
        void* iterator = nullptr;
        size_t count = 0;
        while (auto* interface = api_.class_get_interfaces(klass, &iterator)) {
            if (out.tellp() > kMaxClassBytes) {
                error = "class output size limit exceeded";
                return false;
            }
            if (++count > kMaxMembersPerClass) {
                error = "interface iterator limit exceeded";
                return false;
            }
            out << (parent_written ? ", " : " : ") << type_name(api_.class_get_type(interface));
            parent_written = true;
        }
        out << "\n{\n\t// Fields\n";
        if (!fields(out, klass, is_enum))
            return false;
        out << "\n\t// Properties\n";
        if (!properties(out, klass))
            return false;
        out << "\n\t// Methods\n";
        if (!methods(out, klass))
            return false;
        out << "}\n";
        if (out.tellp() > kMaxClassBytes) {
            error = "class output size limit exceeded";
            return false;
        }
        if (!error.empty())
            return false;
        auto text = out.str();
        if (text.size() > kMaxOutputBytes - std::min(kMaxOutputBytes, output_.bytes())) {
            error = "output size limit exceeded";
            return false;
        }
        if (!output_.write(text)) {
            error = output_.error();
            return false;
        }
        ++classes;
        return true;
    }

    bool reflection_image(const Il2CppImage* image, const std::string& image_name) {
        auto* assembly_class =
            api_.class_from_name(api_.get_corlib(), "System.Reflection", "Assembly");
        if (!assembly_class) {
            error = "System.Reflection.Assembly unavailable";
            return false;
        }
        const MethodInfo* load = nullptr;
        void* iterator = nullptr;
        size_t count = 0;
        while (const auto* method = api_.class_get_methods(assembly_class, &iterator)) {
            if (++count > kMaxMembersPerClass) {
                error = "reflection method iterator limit exceeded";
                return false;
            }
            if (name(api_.method_get_name(method)) == "Load" &&
                api_.method_get_param_count(method) == 1 &&
                type_name(api_.method_get_param(method, 0)) == "System.String") {
                load = method;
                break;
            }
        }
        auto simple_name = image_name.substr(0, image_name.rfind('.'));
        void* args[] = {api_.string_new(simple_name.c_str())};
        auto* assembly = invoke(load, nullptr, args, "Assembly.Load(string)");
        if (!assembly) {
            if (error.empty())
                error = "Assembly.Load returned null";
            return false;
        }
        auto* klass = api_.object_get_class(assembly);
        if (!klass) {
            error = "reflection assembly has no class";
            return false;
        }
        auto* types = invoke(api_.class_get_method_from_name(klass, "GetTypes", 0), assembly,
                             nullptr, "Assembly.GetTypes");
        if (!types) {
            if (error.empty())
                error = "Assembly.GetTypes returned null";
            return false;
        }
        auto* array_class = api_.object_get_class(types);
        if (!array_class) {
            error = "reflection array has no class";
            return false;
        }
        auto* enumerator = invoke(api_.class_get_method_from_name(array_class, "GetEnumerator", 0),
                                  types, nullptr, "Array.GetEnumerator");
        if (!enumerator) {
            if (error.empty())
                error = "Array.GetEnumerator returned null";
            return false;
        }
        auto* enum_class = api_.object_get_class(enumerator);
        if (!enum_class) {
            error = "enumerator has no class";
            return false;
        }
        const auto* move_next = api_.class_get_method_from_name(enum_class, "MoveNext", 0);
        const auto* current = api_.class_get_method_from_name(enum_class, "get_Current", 0);
        for (size_t index = 0; index <= kMaxClassesPerImage; ++index) {
            auto* boxed = invoke(move_next, enumerator, nullptr, "IEnumerator.MoveNext");
            if (!boxed) {
                if (error.empty())
                    error = "MoveNext returned null";
                return false;
            }
            const auto value =
                memory_.read<uint8_t>(reinterpret_cast<uintptr_t>(api_.object_unbox(boxed)));
            if (!value || *value > 1) {
                error = "invalid MoveNext boolean";
                return false;
            }
            if (*value == 0)
                return true;
            if (index == kMaxClassesPerImage)
                break;
            auto* type = invoke(current, enumerator, nullptr, "IEnumerator.Current");
            if (!type || !write_type(api_.class_from_system_type(
                                         reinterpret_cast<Il2CppReflectionType*>(type)),
                                     image_name))
                return false;
        }
        (void)image;
        error = "reflection class limit exceeded";
        return false;
    }

    size_t classes{}, method_count{}, invalid_names{}, missing_addresses{};
    std::string error;

  private:
    bool fields(std::ostringstream& out, Il2CppClass* klass, bool is_enum) {
        void* iterator = nullptr;
        size_t count = 0;
        while (auto* field = api_.class_get_fields(klass, &iterator)) {
            if (out.tellp() > kMaxClassBytes) {
                error = "class output size limit exceeded";
                return false;
            }
            if (++count > kMaxMembersPerClass) {
                error = "field iterator limit exceeded";
                return false;
            }
            const auto flags = static_cast<uint32_t>(api_.field_get_flags(field));
            out << '\t' << access_modifier(flags, false);
            if (flags & FIELD_ATTRIBUTE_LITERAL)
                out << "const ";
            else {
                if (flags & FIELD_ATTRIBUTE_STATIC)
                    out << "static ";
                if (flags & FIELD_ATTRIBUTE_INIT_ONLY)
                    out << "readonly ";
            }
            out << type_name(api_.field_get_type(field)) << ' ' << name(api_.field_get_name(field));
            if (is_enum && (flags & FIELD_ATTRIBUTE_LITERAL) && api_.class_enum_basetype &&
                api_.field_static_get_value) {
                const auto underlying = type_name(api_.class_enum_basetype(klass));
                std::array<std::byte, 8> value{};
                // Only primitive integral enum storage may be copied into this buffer.
                const std::array<std::string_view, 8> names{
                    "System.SByte", "System.Byte",   "System.Int16", "System.UInt16",
                    "System.Int32", "System.UInt32", "System.Int64", "System.UInt64"};
                const auto found = std::find(names.begin(), names.end(), underlying);
                if (found != names.end()) {
                    api_.field_static_get_value(field, value.data());
                    const auto index = static_cast<size_t>(found - names.begin());
                    const size_t width = size_t{1} << (index / 2);
                    uint64_t integer{};
                    std::memcpy(&integer, value.data(), width);
                    out << " = " << std::dec;
                    if (index % 2 == 0) {
                        if (width < 8 && (integer & (uint64_t{1} << (width * 8 - 1))))
                            integer |= ~((uint64_t{1} << (width * 8)) - 1);
                        int64_t signed_value{};
                        std::memcpy(&signed_value, &integer, sizeof(integer));
                        out << signed_value;
                    } else
                        out << integer;
                }
            }
            const auto offset = api_.field_get_offset(field);
            out << "; // ";
            if (offset == std::numeric_limits<size_t>::max())
                out << "-1 (thread static)";
            else
                out << "0x" << std::hex << offset;
            out << '\n';
        }
        return true;
    }
    bool properties(std::ostringstream& out, Il2CppClass* klass) {
        void* iterator = nullptr;
        size_t count = 0;
        while (const auto* property = api_.class_get_properties(klass, &iterator)) {
            if (out.tellp() > kMaxClassBytes) {
                error = "class output size limit exceeded";
                return false;
            }
            if (++count > kMaxMembersPerClass) {
                error = "property iterator limit exceeded";
                return false;
            }
            auto* mutable_property = const_cast<PropertyInfo*>(property);
            const auto* get = api_.property_get_get_method(mutable_property);
            const auto* set = api_.property_get_set_method(mutable_property);
            const Il2CppType* type = nullptr;
            uint32_t implementation_flags{};
            if (get)
                type = api_.method_get_return_type(get);
            else if (set) {
                const auto n = api_.method_get_param_count(set);
                if (n > kMaxParameters) {
                    error = "invalid setter parameter count";
                    return false;
                }
                if (n > 0)
                    type = api_.method_get_param(set, n - 1);
            }
            if (!type) {
                out << "\t// Unknown property: " << name(api_.property_get_name(mutable_property))
                    << '\n';
                continue;
            }
            out << '\t'
                << method_modifier(api_.method_get_flags(get ? get : set, &implementation_flags))
                << type_name(type) << ' ' << name(api_.property_get_name(mutable_property))
                << " { ";
            if (get)
                out << "get; ";
            if (set)
                out << "set; ";
            out << "}\n";
        }
        return true;
    }
    bool methods(std::ostringstream& out, Il2CppClass* klass) {
        void* iterator = nullptr;
        std::vector<const MethodInfo*> methods;
        while (const auto* method = api_.class_get_methods(klass, &iterator)) {
            if (methods.size() >= kMaxMembersPerClass || Clock::now() > deadline_) {
                error = "method iterator limit/deadline exceeded";
                return false;
            }
            methods.push_back(method);
        }
        std::vector<uintptr_t> pointers(methods.size());
        if (method_offset_) {
            std::vector<uintptr_t> addresses;
            addresses.reserve(methods.size());
            for (const auto* method : methods)
                addresses.push_back(
                    checked_add(untag_address(reinterpret_cast<uintptr_t>(method)), *method_offset_)
                        .value_or(0));
            memory_.read_pointers(addresses, pointers);
        }
        for (size_t method_index = 0; method_index < methods.size(); ++method_index) {
            if (out.tellp() > kMaxClassBytes || Clock::now() > deadline_) {
                error = "class output size/deadline limit exceeded";
                return false;
            }
            const auto* method = methods[method_index];
            const auto pointer = pointers[method_index];
            if (pointer && maps_.executable(pointer)) {
                out << "\t// RVA: ";
                if (pointer >= base_ && same_module(pointer))
                    out << "0x" << std::hex << (pointer - base_);
                else
                    out << "unavailable";
                out << " VA: 0x" << std::hex << pointer << '\n';
            } else {
                ++missing_addresses;
                out << "\t// RVA: unavailable VA: 0x0\n";
            }
            uint32_t implementation_flags{};
            out << '\t' << method_modifier(api_.method_get_flags(method, &implementation_flags));
            const auto* result = api_.method_get_return_type(method);
            if (byref(result))
                out << "ref ";
            out << type_name(result) << ' ' << name(api_.method_get_name(method)) << '(';
            const uint32_t parameters = api_.method_get_param_count(method);
            if (parameters > kMaxParameters) {
                error = "invalid parameter count";
                return false;
            }
            for (uint32_t i = 0; i < parameters; ++i) {
                if (out.tellp() > kMaxClassBytes) {
                    error = "class output size limit exceeded";
                    return false;
                }
                if (i != 0)
                    out << ", ";
                const auto* parameter = api_.method_get_param(method, i);
                const uint32_t attributes =
                    parameter && api_.type_get_attrs ? api_.type_get_attrs(parameter) : 0;
                if (byref(parameter)) {
                    if ((attributes & PARAM_ATTRIBUTE_OUT) && !(attributes & PARAM_ATTRIBUTE_IN))
                        out << "out ";
                    else if ((attributes & PARAM_ATTRIBUTE_IN) &&
                             !(attributes & PARAM_ATTRIBUTE_OUT))
                        out << "in ";
                    else
                        out << "ref ";
                } else {
                    if (attributes & PARAM_ATTRIBUTE_IN)
                        out << "[In] ";
                    if (attributes & PARAM_ATTRIBUTE_OUT)
                        out << "[Out] ";
                }
                out << type_name(parameter) << ' ' << name(api_.method_get_param_name(method, i));
            }
            out << ") { }\n";
            ++method_count;
        }
        return true;
    }
    bool same_module(uintptr_t address) const {
        return api_.module_image.contains(address, 1, elf::kExecute);
    }
    Il2CppApi& api_;
    AtomicOutput& output_;
    uintptr_t base_{};
    Maps maps_;
    Memory memory_;
    std::unordered_map<const Il2CppType*, std::string> type_names_;
    std::unordered_map<const char*, std::string> names_;
    size_t name_cache_bytes_{}, type_cache_bytes_{};
    std::optional<size_t> method_offset_;
    Clock::time_point deadline_{Clock::now() + kDumpTimeout};
};
} // namespace

bool dump_runtime(void* handle, const std::string& data_directory, const DumpOptions& options) {
    Il2CppApi api;
    std::string error;
    if (!api.load(handle, error)) {
        LOGE("stage=resolve fatal=%s", error.c_str());
        return false;
    }
    xdl_info_t info{};
    if (xdl_info(handle, XDL_DI_DLINFO, &info) != 0 || !info.dli_fbase) {
        LOGE("stage=resolve fatal=missing-load-bias");
        return false;
    }
    LOGI("module=%s load_bias=%p phdrs=%zu", info.dli_fname ? info.dli_fname : "<unknown>",
         info.dli_fbase, info.dlpi_phnum);
    const auto deadline = Clock::now() + std::chrono::seconds(options.timeout_seconds);
#if defined(__aarch64__)
    LOGI("stage=initialize readiness=validated-runtime-getters");
    Memory readiness_memory;
    unsigned stable_samples = 0;
    // Reading validated accessor globals avoids executing a partially loaded or
    // temporarily encrypted API. Corlib alone can precede GC/domain readiness.
    do {
        const auto maps = Maps::read_self();
        const auto read_code = [&](uintptr_t address, void* target, size_t size) {
            return maps.executable(address) && readiness_memory.read(address, target, size);
        };
        const auto corlib =
            aarch64_pointer_getter(reinterpret_cast<uintptr_t>(api.get_corlib), read_code);
        const auto domain =
            aarch64_pointer_getter(reinterpret_cast<uintptr_t>(api.domain_get), read_code);
        const auto valid_global = [&](const std::optional<PointerGetter>& getter) {
            if (!getter)
                return false;
            auto address = getter->address;
            const auto* mapping = maps.find(address, sizeof(uintptr_t));
            if (!mapping || !mapping->readable || mapping->executable)
                return false;
            if (getter->base_offset) {
                const auto base = readiness_memory.read<uintptr_t>(address).value_or(0);
                const auto slot = checked_add(untag_address(base), *getter->base_offset);
                if (!base || !slot)
                    return false;
                address = *slot;
                mapping = maps.find(address, sizeof(uintptr_t));
                if (!mapping || !mapping->readable || mapping->executable)
                    return false;
            }
            const auto value =
                getter->indirect ? readiness_memory.read<uintptr_t>(address).value_or(0) : address;
            return value != 0 && maps.readable(value, sizeof(uintptr_t));
        };
        bool thread_ready = false;
        if (api.is_vm_thread) {
            const auto chain =
                aarch64_vm_thread_chain(reinterpret_cast<uintptr_t>(api.is_vm_thread), read_code);
            if (chain && maps.readable(chain->global, sizeof(uintptr_t))) {
                const auto owner = readiness_memory.read<uintptr_t>(chain->global).value_or(0);
                const auto field = checked_add(untag_address(owner), chain->offset);
                if (owner && field && maps.readable(*field, sizeof(uintptr_t))) {
                    const auto thread = readiness_memory.read<uintptr_t>(*field).value_or(0);
                    thread_ready = thread != 0 && maps.readable(thread, sizeof(uintptr_t));
                }
            }
        }
        // Some Unity releases create AppDomain lazily on the first query. The
        // validated VM-thread predicate provides an independent readiness gate.
        const bool corlib_ready = valid_global(corlib), domain_ready = valid_global(domain);
        if (corlib_ready && (domain_ready || thread_ready))
            ++stable_samples;
        else
            stable_samples = 0;
        if (stable_samples >= 2) {
            LOGI("stage=initialize ready corlib_GOT=%d domain_GOT=%d",
                 corlib->base_offset.has_value(), domain && domain->base_offset.has_value());
            break;
        }
        if (Clock::now() >= deadline) {
            LOGE("stage=initialize fatal=runtime-getters-unready-or-unsupported "
                 "corlib_profile=%d corlib_ready=%d domain_profile=%d domain_ready=%d "
                 "thread_ready=%d",
                 corlib.has_value(), corlib_ready, domain.has_value(), domain_ready, thread_ready);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } while (true);
#else
    LOGI("stage=initialize readiness=corlib-settle");
    while (!api.get_corlib()) {
        if (Clock::now() >= deadline) {
            LOGE("stage=initialize fatal=corlib-timeout");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
#endif
    auto* domain = api.domain_get();
    if (!domain) {
        LOGE("stage=initialize fatal=null-domain");
        return false;
    }
    auto* previous_thread = api.thread_current ? api.thread_current() : nullptr;
    auto* thread = previous_thread ? previous_thread : api.thread_attach(domain);
    if (!thread) {
        LOGE("stage=initialize fatal=thread-attach-failed");
        return false;
    }
    struct Attachment {
        Il2CppApi& api;
        Il2CppThread* thread;
        bool owned;
        ~Attachment() {
            if (owned)
                api.thread_detach(thread);
        }
    } attachment{api, thread, previous_thread == nullptr};
    Memory memory;
    std::vector<const Il2CppAssembly*> snapshot;
    if (!snapshot_assemblies(api, domain, memory, snapshot, error)) {
        LOGE("stage=enumerate fatal=%s", error.c_str());
        return false;
    }
    const size_t count = snapshot.size();
    const std::string directory = data_directory + "/files";
    if (mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
        LOGE("stage=output fatal=mkdir-failed errno=%d", errno);
        return false;
    }
    auto filename = std::string("dump.cs");
    if (const auto colon = options.process.find(':'); colon != std::string::npos) {
        auto suffix = options.process.substr(colon + 1);
        for (char& c : suffix)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_'))
                c = '_';
        filename = "dump-" + suffix + ".cs";
    }
    AtomicOutput output(directory, filename);
    if (!output.good()) {
        LOGE("stage=output fatal=%s", output.error().c_str());
        return false;
    }
    const auto started = Clock::now();
    Writer writer(api, output, reinterpret_cast<uintptr_t>(info.dli_fbase));
    writer.calibrate_method_layout();
    output.write("// Zygisk-Il2CppDumper: live runtime API dump\n// Metadata and registrations are "
                 "supplied by IL2CPP; no private metadata layout is assumed.\n");
    struct ImageSnapshot {
        const Il2CppImage* image;
        std::string name;
    };
    std::vector<ImageSnapshot> images;
    images.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (!snapshot[i]) {
            LOGE("stage=enumerate fatal=null-assembly index=%zu", i);
            return false;
        }
        const auto* image = api.assembly_get_image(snapshot[i]);
        if (!image) {
            LOGE("stage=enumerate fatal=null-image index=%zu", i);
            return false;
        }
        images.push_back({image, writer.name(api.image_get_name(image))});
        if (!output.write("// Image " + std::to_string(i) + ": " + images.back().name + "\n")) {
            LOGE("stage=output fatal=%s", output.error().c_str());
            return false;
        }
    }
    const bool image_api = api.image_get_class && api.image_get_class_count;
    bool metadata_found = false;
    const auto metadata_maps = Maps::read_self();
    for (const auto& mapping : metadata_maps.entries()) {
        if (!mapping.readable || mapping.file_offset != 0 ||
            mapping.path.find("global-metadata.dat") == std::string::npos)
            continue;
        const auto header = memory.read<MetadataHeader>(mapping.begin);
        if (header && header->magic == 0xfab11bafU && header->version != 0) {
            LOGI("metadata_source=%s metadata_version=%u (header identification only)",
                 mapping.path.c_str(), header->version);
            metadata_found = true;
        }
    }
    if (!metadata_found)
        LOGI("metadata_source=runtime (no named metadata mapping)");
    LOGI("registrations=owned-by-runtime private-registration-layout=unused");
    LOGI("stage=dump strategy=%s assemblies=%zu", image_api ? "image-API" : "managed-reflection",
         count);
    for (const auto& entry : images) {
        const auto* image = entry.image;
        const auto& image_name = entry.name;
        if (image_api) {
            const size_t class_count = api.image_get_class_count(image);
            if (class_count > kMaxClassesPerImage) {
                LOGE("stage=enumerate fatal=invalid-class-count count=%zu image=%s", class_count,
                     image_name.c_str());
                return false;
            }
            if (options.verbose)
                LOGI("stage=image name=%s classes=%zu", image_name.c_str(), class_count);
            for (size_t index = 0; index < class_count; ++index) {
                if (!writer.write_type(const_cast<Il2CppClass*>(api.image_get_class(image, index)),
                                       image_name)) {
                    LOGE("stage=dump fatal=%s image=%s class_index=%zu", writer.error.c_str(),
                         image_name.c_str(), index);
                    return false;
                }
            }
        } else if (!writer.reflection_image(image, image_name)) {
            LOGE("stage=reflection fatal=%s", writer.error.c_str());
            return false;
        }
    }
    const auto before_publish = Clock::now();
    if (!output.commit()) {
        LOGE("stage=output fatal=%s", output.error().c_str());
        return false;
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    LOGI("stage=timing format_ms=%lld publish_ms=%lld",
         static_cast<long long>(
             std::chrono::duration_cast<std::chrono::milliseconds>(before_publish - started)
                 .count()),
         static_cast<long long>(
             std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - before_publish)
                 .count()));
    LOGI("stage=complete path=%s/%s classes=%zu methods=%zu bytes=%zu elapsed_ms=%lld "
         "unavailable_addresses=%zu invalid_names=%zu",
         directory.c_str(), filename.c_str(), writer.classes, writer.method_count, output.bytes(),
         static_cast<long long>(elapsed), writer.missing_addresses, writer.invalid_names);
    return true;
}
} // namespace dumper
