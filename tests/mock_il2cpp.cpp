// Synthetic runtime for integration testing; contains no Unity/game source or assets.
#include "il2cpp-class.h"
#include <array>
#include <cstdint>
#include <cstring>
#ifndef FIXTURE_LAYOUT_WORDS
#define FIXTURE_LAYOUT_WORDS 0
#endif
#define EXPORTED extern "C" __attribute__((visibility("default")))
#define HIDDEN_API extern "C" __attribute__((visibility("hidden"), used, retain))
#ifdef FIXTURE_HIDE_CAPABILITIES
#define ENUMERATION_API HIDDEN_API
#else
#define ENUMERATION_API EXPORTED
#endif
#if defined(FIXTURE_HIDE_CAPABILITIES) || defined(FIXTURE_HIDE_CALIBRATION)
#define CALIBRATION_API HIDDEN_API
#else
#define CALIBRATION_API EXPORTED
#endif
struct Il2CppDomain {
    uintptr_t marker{42};
};
struct Il2CppThread {};
struct Il2CppImage {
    const char* name;
};
struct Il2CppAssembly {
    const Il2CppImage* image;
};
struct Il2CppClass {
    const char* name;
    const char* space;
    int kind;
    Il2CppType* type{};
};
struct Il2CppType {
    Il2CppClass* klass;
    uint32_t attrs{};
    bool byref{};
};
struct Il2CppObject {
    Il2CppClass* klass;
    uintptr_t value{};
    size_t cursor{};
};
struct Il2CppString {
    const char* text;
};
struct MethodInfo {
#if FIXTURE_LAYOUT_WORDS > 0
    uintptr_t padding[FIXTURE_LAYOUT_WORDS]{};
#endif
    void (*pointer)();
    const char* name;
    const Il2CppType* result;
    int kind{};
    uint32_t parameters{};
    std::array<const Il2CppType*, 2> types{};
};
struct PropertyInfo {
    const char* name;
    const MethodInfo* get;
    const MethodInfo* set;
};
struct FieldInfo {
    const char* name;
    const Il2CppType* type;
    uint32_t flags;
    size_t offset;
    int kind;
};
namespace {
Il2CppClass intptr_class{"IntPtr", "System", 0}, int_class{"Int32", "System", 0},
    string_class{"String", "System", 0}, void_class{"Void", "System", 0},
    bool_class{"Boolean", "System", 0}, player_class{"Player", "Fixture", 1},
    state_class{"State", "Fixture", 2}, module_class{"Module", "System.Reflection", 3},
    delegate_class{"Delegate", "System", 4}, assembly_class{"Assembly", "System.Reflection", 5},
    array_class{"Array", "System", 6}, enumerator_class{"ArrayEnumerator", "System", 7},
    reflection_class{"RuntimeType", "System", 8};
Il2CppType intptr_type{&intptr_class}, int_type{&int_class}, string_type{&string_class},
    void_type{&void_class}, bool_type{&bool_class}, player_type{&player_class},
    state_type{&state_class}, out_type{&int_class, 2, true};
Il2CppImage image{"Fixture.dll"};
Il2CppAssembly assembly{&image};
const Il2CppAssembly* assemblies[]{&assembly};
const Il2CppAssembly* alternate_assemblies[]{&assembly};
unsigned assembly_queries{};
Il2CppDomain domain;
Il2CppDomain* current_domain = &domain;
const Il2CppImage* corlib = &image;
Il2CppThread thread;
int attached{}, mode{};
volatile int sink{};
void first_code() { sink = 1; }
void second_code() { sink = 2; }
MethodInfo make_method(const char* name, const Il2CppType* result, int kind = 0, uint32_t count = 0,
                       const Il2CppType* first = nullptr, const Il2CppType* second = nullptr) {
    MethodInfo method{};
    method.pointer = kind == 20 ? second_code : first_code;
    method.name = name;
    method.result = result;
    method.kind = kind;
    method.parameters = count;
    method.types = {first, second};
    return method;
}
MethodInfo get_name = make_method("get_Name", &string_type),
           set_name = make_method("set_Name", &void_type, 0, 1, &string_type),
           try_parse = make_method("TryParse", &bool_type, 0, 2, &string_type, &out_type),
           first_filter = make_method("FilterTypeName", &bool_type, 19),
           second_filter = make_method("FilterTypeNameIgnoreCase", &bool_type, 20),
           load = make_method("Load", &player_type, 1, 1, &string_type),
           get_types = make_method("GetTypes", &player_type, 2),
           get_enumerator = make_method("GetEnumerator", &player_type, 3),
           move_next = make_method("MoveNext", &bool_type, 4),
           get_current = make_method("get_Current", &player_type, 5);
PropertyInfo name_property{"Name", &get_name, &set_name};
FieldInfo player_id{"id", &int_type, 6, 16, 0}, enum_negative{"Negative", &state_type, 0x56, 0, 1},
    filter_first{"FilterTypeName", &player_type, 0x16, 0, 2},
    filter_second{"FilterTypeNameIgnoreCase", &player_type, 0x16, 0, 3},
    delegate_pointer{"method_ptr", &intptr_type, 6, 0, 4},
    delegate_method{"method", &intptr_type, 6, 0, 5};
Il2CppObject delegate_first{&delegate_class, reinterpret_cast<uintptr_t>(&first_filter)},
    delegate_second{&delegate_class, reinterpret_cast<uintptr_t>(&second_filter)},
    reflection_assembly{&assembly_class}, reflection_array{&array_class},
    enumerator{&enumerator_class},
    reflected_player{&reflection_class, reinterpret_cast<uintptr_t>(&player_class)},
    reflected_state{&reflection_class, reinterpret_cast<uintptr_t>(&state_class)},
    boxed{&bool_class};
uint8_t bool_value{};
Il2CppString string_value{};
template <class T, size_t N> T* iterate(const std::array<T*, N>& values, void** iterator) {
    const auto index = reinterpret_cast<uintptr_t>(*iterator);
    if (index >= values.size())
        return nullptr;
    *iterator = reinterpret_cast<void*>(index + 1);
    return values[index];
}
} // namespace
EXPORTED void fixture_configure(int value) {
    mode = value;
    assembly_queries = 0;
    attached = 0;
    corlib = value == 4 ? nullptr : &image;
    current_domain = value == 4 ? nullptr : &domain;
    int_class.type = &int_type;
    string_class.type = &string_type;
    void_class.type = &void_type;
    bool_class.type = &bool_type;
    player_class.type = &player_type;
    state_class.type = &state_type;
}
EXPORTED int fixture_attached() { return attached; }
EXPORTED uintptr_t fixture_method_address() { return reinterpret_cast<uintptr_t>(first_code); }
EXPORTED const Il2CppImage* il2cpp_get_corlib() { return corlib; }
EXPORTED Il2CppDomain* il2cpp_domain_get() { return current_domain; }
EXPORTED const Il2CppAssembly** il2cpp_domain_get_assemblies(const Il2CppDomain*, size_t* size) {
    *size = 1;
    ++assembly_queries;
    if ((mode == 7 && assembly_queries % 2 == 0) || (mode == 8 && assembly_queries > 1))
        return alternate_assemblies;
    return assemblies;
}
EXPORTED const Il2CppImage* il2cpp_assembly_get_image(const Il2CppAssembly* value) {
    return value->image;
}
EXPORTED const char* il2cpp_image_get_name(const Il2CppImage* value) { return value->name; }
EXPORTED Il2CppThread* il2cpp_thread_attach(Il2CppDomain*) {
    ++attached;
    return &thread;
}
EXPORTED void il2cpp_thread_detach(Il2CppThread*) { --attached; }
EXPORTED Il2CppThread* il2cpp_thread_current() { return nullptr; }
EXPORTED Il2CppClass* il2cpp_class_from_type(const Il2CppType* type) {
    return type ? type->klass : nullptr;
}
EXPORTED const Il2CppType* il2cpp_class_get_type(Il2CppClass* klass) { return klass->type; }
#if defined(FIXTURE_HIDE_CORE)
extern "C" __attribute__((visibility("hidden"), used, retain))
const char* il2cpp_class_get_name(Il2CppClass* klass) {
    return klass->name;
}
#elif !defined(FIXTURE_OMIT_CORE)
EXPORTED const char* il2cpp_class_get_name(Il2CppClass* klass) { return klass->name; }
#endif
EXPORTED const char* il2cpp_class_get_namespace(Il2CppClass* klass) { return klass->space; }
EXPORTED int il2cpp_class_get_flags(const Il2CppClass*) { return 1; }
EXPORTED bool il2cpp_class_is_valuetype(const Il2CppClass* klass) { return klass->kind == 2; }
EXPORTED bool il2cpp_class_is_enum(const Il2CppClass* klass) { return klass->kind == 2; }
EXPORTED Il2CppClass* il2cpp_class_get_parent(Il2CppClass*) { return nullptr; }
EXPORTED Il2CppClass* il2cpp_class_get_interfaces(Il2CppClass*, void**) { return nullptr; }
EXPORTED FieldInfo* il2cpp_class_get_fields(Il2CppClass* klass, void** iterator) {
    if (klass->kind == 1)
        return iterate(std::array{&player_id}, iterator);
    if (klass->kind == 2)
        return iterate(std::array{&enum_negative}, iterator);
    return nullptr;
}
EXPORTED const PropertyInfo* il2cpp_class_get_properties(Il2CppClass* klass, void** iterator) {
    return klass->kind == 1 ? iterate(std::array{&name_property}, iterator) : nullptr;
}
EXPORTED const MethodInfo* il2cpp_class_get_methods(Il2CppClass* klass, void** iterator) {
    if (klass->kind == 1)
        return iterate(std::array{&get_name, &set_name, &try_parse}, iterator);
    if (klass->kind == 5)
        return iterate(std::array{&load, &get_types}, iterator);
    return nullptr;
}
EXPORTED uint32_t il2cpp_method_get_flags(const MethodInfo* method, uint32_t* flags) {
    *flags = 0;
    return method->kind == 1 ? 0x16 : 6;
}
EXPORTED const Il2CppType* il2cpp_method_get_return_type(const MethodInfo* method) {
    return method->result;
}
EXPORTED const char* il2cpp_method_get_name(const MethodInfo* method) { return method->name; }
EXPORTED uint32_t il2cpp_method_get_param_count(const MethodInfo* method) {
    return method->parameters;
}
EXPORTED const Il2CppType* il2cpp_method_get_param(const MethodInfo* method, uint32_t index) {
    return index < method->parameters ? method->types[index] : nullptr;
}
EXPORTED const char* il2cpp_method_get_param_name(const MethodInfo*, uint32_t index) {
    return index == 0 ? "value" : "result";
}
EXPORTED const MethodInfo* il2cpp_property_get_get_method(PropertyInfo* property) {
    return property->get;
}
EXPORTED const MethodInfo* il2cpp_property_get_set_method(PropertyInfo* property) {
    return property->set;
}
EXPORTED const char* il2cpp_property_get_name(PropertyInfo* property) { return property->name; }
EXPORTED int il2cpp_field_get_flags(FieldInfo* field) { return static_cast<int>(field->flags); }
EXPORTED const Il2CppType* il2cpp_field_get_type(FieldInfo* field) { return field->type; }
EXPORTED const char* il2cpp_field_get_name(FieldInfo* field) { return field->name; }
EXPORTED size_t il2cpp_field_get_offset(FieldInfo* field) { return field->offset; }
#ifndef FIXTURE_LEGACY
ENUMERATION_API size_t il2cpp_image_get_class_count(const Il2CppImage*) {
    return mode == 2 ? 1000001 : 2;
}
ENUMERATION_API const Il2CppClass* il2cpp_image_get_class(const Il2CppImage*, size_t index) {
    return index == 0 ? &player_class : &state_class;
}
#endif
EXPORTED bool il2cpp_type_is_byref(const Il2CppType* type) { return type->byref; }
EXPORTED uint32_t il2cpp_type_get_attrs(const Il2CppType* type) { return type->attrs; }
EXPORTED const Il2CppType* il2cpp_class_enum_basetype(Il2CppClass*) { return &int_type; }
EXPORTED void il2cpp_field_static_get_value(FieldInfo* field, void* destination) {
    if (field->kind == 1) {
        const int32_t value = -1;
        std::memcpy(destination, &value, sizeof(value));
    } else {
        auto* value = field->kind == 2 ? &delegate_first : &delegate_second;
        std::memcpy(destination, &value, sizeof(value));
    }
}
EXPORTED Il2CppClass* il2cpp_class_from_name(const Il2CppImage*, const char*, const char* name) {
    if (std::strcmp(name, "Module") == 0)
        return &module_class;
    if (std::strcmp(name, "Assembly") == 0)
        return &assembly_class;
    return nullptr;
}
EXPORTED const MethodInfo* il2cpp_class_get_method_from_name(Il2CppClass*, const char* name, int) {
    for (auto* method : {&load, &get_types, &get_enumerator, &move_next, &get_current})
        if (std::strcmp(name, method->name) == 0)
            return method;
    return nullptr;
}
EXPORTED FieldInfo* il2cpp_class_get_field_from_name(Il2CppClass*, const char* name) {
    if (mode == 6)
        return nullptr;
    for (auto* field : {&filter_first, &filter_second, &delegate_pointer, &delegate_method})
        if (std::strcmp(name, field->name) == 0)
            return field;
    return nullptr;
}
EXPORTED Il2CppObject* il2cpp_runtime_invoke(const MethodInfo* method, void*, void**,
                                             Il2CppException** exception) {
    *exception = nullptr;
    if (mode == 3 && method->kind == 2) {
        *exception = reinterpret_cast<Il2CppException*>(1);
        return nullptr;
    }
    switch (method->kind) {
    case 1:
        return &reflection_assembly;
    case 2:
        return &reflection_array;
    case 3:
        enumerator.cursor = 0;
        return &enumerator;
    case 4:
        bool_value = enumerator.cursor++ < 2 ? 1 : 0;
        return &boxed;
    case 5:
        return enumerator.cursor == 1 ? &reflected_player : &reflected_state;
    default:
        return nullptr;
    }
}
ENUMERATION_API Il2CppClass* il2cpp_class_from_system_type(Il2CppReflectionType* type) {
    return reinterpret_cast<Il2CppClass*>(reinterpret_cast<Il2CppObject*>(type)->value);
}
EXPORTED Il2CppString* il2cpp_string_new(const char* value) {
    string_value.text = value;
    return &string_value;
}
EXPORTED Il2CppClass* il2cpp_object_get_class(Il2CppObject* value) { return value->klass; }
EXPORTED const MethodInfo* il2cpp_object_get_virtual_method(Il2CppObject*,
                                                            const MethodInfo* method) {
    return method;
}
EXPORTED void* il2cpp_object_unbox(Il2CppObject*) { return &bool_value; }
EXPORTED void il2cpp_field_get_value(Il2CppObject* object, FieldInfo* field, void* destination) {
    auto* method = reinterpret_cast<MethodInfo*>(object->value);
    const uintptr_t value =
        field->kind == 4 ? reinterpret_cast<uintptr_t>(method->pointer) : object->value;
    std::memcpy(destination, &value, sizeof(value));
}
CALIBRATION_API void il2cpp_runtime_class_init(Il2CppClass*) {}
