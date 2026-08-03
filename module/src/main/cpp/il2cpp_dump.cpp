//
// Created by Perfare on 2020/7/4.
//

#include "il2cpp_dump.h"
#include <dlfcn.h>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <cstdio>
#include <unistd.h>
#include "xdl.h"
#include "log.h"
#include "il2cpp-tabledefs.h"
#include "il2cpp-class.h"
#include "semantic_dump.h"

#define DO_API(r, n, p) r (*n) p

#include "il2cpp-api-functions.h"

#undef DO_API

uint64_t il2cpp_base = 0;

void init_il2cpp_api(void *handle) {
#define DO_API(r, n, p) {                      \
    n = (r (*) p)xdl_sym(handle, #n, nullptr); \
    if(!n) {                                   \
        LOGW("api not found %s", #n);          \
    }                                          \
}

#include "il2cpp-api-functions.h"

#undef DO_API
}

std::string get_method_modifier(uint32_t flags) {
    std::stringstream outPut;
    auto access = flags & METHOD_ATTRIBUTE_MEMBER_ACCESS_MASK;
    switch (access) {
        case METHOD_ATTRIBUTE_PRIVATE:
            outPut << "private ";
            break;
        case METHOD_ATTRIBUTE_PUBLIC:
            outPut << "public ";
            break;
        case METHOD_ATTRIBUTE_FAMILY:
            outPut << "protected ";
            break;
        case METHOD_ATTRIBUTE_ASSEM:
        case METHOD_ATTRIBUTE_FAM_AND_ASSEM:
            outPut << "internal ";
            break;
        case METHOD_ATTRIBUTE_FAM_OR_ASSEM:
            outPut << "protected internal ";
            break;
    }
    if (flags & METHOD_ATTRIBUTE_STATIC) {
        outPut << "static ";
    }
    if (flags & METHOD_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) {
            outPut << "override ";
        }
    } else if (flags & METHOD_ATTRIBUTE_FINAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) {
            outPut << "sealed override ";
        }
    } else if (flags & METHOD_ATTRIBUTE_VIRTUAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_NEW_SLOT) {
            outPut << "virtual ";
        } else {
            outPut << "override ";
        }
    }
    if (flags & METHOD_ATTRIBUTE_PINVOKE_IMPL) {
        outPut << "extern ";
    }
    return outPut.str();
}

bool _il2cpp_type_is_byref(const Il2CppType *type) {
    if (!type) return false;
    auto byref = type->byref;
    if (il2cpp_type_is_byref) {
        byref = il2cpp_type_is_byref(type);
    }
    return byref;
}

std::string escape_csharp_string(const Il2CppChar *chars, int32_t length) {
    std::stringstream output;
    for (int32_t i = 0; chars && i < length; ++i) {
        const uint16_t c = chars[i];
        switch (c) {
            case '\\': output << "\\\\"; break;
            case '"': output << "\\\""; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            case '\0': output << "\\0"; break;
            default:
                if (c >= 0x20 && c <= 0x7e) {
                    output << static_cast<char>(c);
                } else {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << c;
                }
        }
    }
    return output.str();
}

bool get_literal_value(FieldInfo *field, const Il2CppType *field_type, std::string &value) {
    if (!field || !field_type || !il2cpp_field_static_get_value) return false;

    const Il2CppType *storage_type = field_type;
    if (auto klass = il2cpp_class_from_type(field_type);
        klass && il2cpp_class_is_enum(klass) && il2cpp_class_enum_basetype) {
        const Il2CppType *enum_type = il2cpp_class_enum_basetype(klass);
        if (enum_type) storage_type = enum_type;
    }

    std::stringstream output;
    switch (storage_type->type) {
        case IL2CPP_TYPE_BOOLEAN: {
            bool item = false;
            il2cpp_field_static_get_value(field, &item);
            value = item ? "true" : "false";
            return true;
        }
        case IL2CPP_TYPE_CHAR: {
            Il2CppChar item = 0;
            il2cpp_field_static_get_value(field, &item);
            output << "'\\u" << std::hex << std::setw(4) << std::setfill('0') << item << "'";
            value = output.str();
            return true;
        }
        case IL2CPP_TYPE_I1: {
            int8_t item = 0; il2cpp_field_static_get_value(field, &item);
            value = std::to_string(item); return true;
        }
        case IL2CPP_TYPE_U1: {
            uint8_t item = 0; il2cpp_field_static_get_value(field, &item);
            value = std::to_string(item); return true;
        }
        case IL2CPP_TYPE_I2: {
            int16_t item = 0; il2cpp_field_static_get_value(field, &item);
            value = std::to_string(item); return true;
        }
        case IL2CPP_TYPE_U2: {
            uint16_t item = 0; il2cpp_field_static_get_value(field, &item);
            value = std::to_string(item); return true;
        }
        case IL2CPP_TYPE_I4: {
            int32_t item = 0; il2cpp_field_static_get_value(field, &item);
            value = std::to_string(item); return true;
        }
        case IL2CPP_TYPE_U4: {
            uint32_t item = 0; il2cpp_field_static_get_value(field, &item);
            value = std::to_string(item) + "u"; return true;
        }
        case IL2CPP_TYPE_I8: {
            int64_t item = 0; il2cpp_field_static_get_value(field, &item);
            value = std::to_string(item) + "L"; return true;
        }
        case IL2CPP_TYPE_U8: {
            uint64_t item = 0; il2cpp_field_static_get_value(field, &item);
            value = std::to_string(item) + "UL"; return true;
        }
        case IL2CPP_TYPE_R4: {
            float item = 0; il2cpp_field_static_get_value(field, &item);
            output << item << 'f'; value = output.str(); return true;
        }
        case IL2CPP_TYPE_R8: {
            double item = 0; il2cpp_field_static_get_value(field, &item);
            output << item; value = output.str(); return true;
        }
        case IL2CPP_TYPE_STRING: {
            Il2CppString *item = nullptr;
            il2cpp_field_static_get_value(field, &item);
            value = item ? "\"" + escape_csharp_string(item->chars, item->length) + "\"" : "null";
            return true;
        }
        default:
            return false;
    }
}

std::string dump_method(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Methods\n";
    const bool is_interface = (il2cpp_class_get_flags(klass) & TYPE_ATTRIBUTE_INTERFACE) != 0;
    void *iter = nullptr;
    while (auto method = il2cpp_class_get_methods(klass, &iter)) {
        //TODO attribute
        uint32_t iflags = 0;
        auto flags = il2cpp_method_get_flags(method, &iflags);
        const bool has_body = !(flags & METHOD_ATTRIBUTE_ABSTRACT) && !is_interface &&
                              !(flags & METHOD_ATTRIBUTE_PINVOKE_IMPL);
        if (method->methodPointer && has_body) {
            outPut << "\t// RVA: 0x";
            outPut << std::hex << (uint64_t) method->methodPointer - il2cpp_base;
            outPut << " VA: 0x";
            outPut << std::hex << (uint64_t) method->methodPointer;
        } else {
            outPut << "\t// RVA: unavailable";
        }
        /*if (method->slot != 65535) {
            outPut << " Slot: " << std::dec << method->slot;
        }*/
        outPut << "\n\t";
        outPut << get_method_modifier(flags);
        //TODO genericContainerIndex
        auto return_type = il2cpp_method_get_return_type(method);
        if (_il2cpp_type_is_byref(return_type)) {
            outPut << "ref ";
        }
        outPut << il2cpp_type_display_name(return_type, false) << " " << il2cpp_method_get_name(method)
               << "(";
        auto param_count = il2cpp_method_get_param_count(method);
        for (int i = 0; i < param_count; ++i) {
            auto param = il2cpp_method_get_param(method, i);
            auto attrs = param->attrs;
            if (_il2cpp_type_is_byref(param)) {
                if (attrs & PARAM_ATTRIBUTE_OUT && !(attrs & PARAM_ATTRIBUTE_IN)) {
                    outPut << "out ";
                } else if (attrs & PARAM_ATTRIBUTE_IN && !(attrs & PARAM_ATTRIBUTE_OUT)) {
                    outPut << "in ";
                } else {
                    outPut << "ref ";
                }
            } else {
                if (attrs & PARAM_ATTRIBUTE_IN) {
                    outPut << "[In] ";
                }
                if (attrs & PARAM_ATTRIBUTE_OUT) {
                    outPut << "[Out] ";
                }
            }
            outPut << il2cpp_type_display_name(param, false) << " "
                   << il2cpp_method_get_param_name(method, i);
            outPut << ", ";
        }
        if (param_count > 0) {
            outPut.seekp(-2, outPut.cur);
        }
        outPut << (has_body ? ") { }\n" : ");\n");
        //TODO GenericInstMethod
    }
    return outPut.str();
}

std::string dump_property(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Properties\n";
    void *iter = nullptr;
    while (auto prop_const = il2cpp_class_get_properties(klass, &iter)) {
        //TODO attribute
        auto prop = const_cast<PropertyInfo *>(prop_const);
        auto get = il2cpp_property_get_get_method(prop);
        auto set = il2cpp_property_get_set_method(prop);
        auto prop_name = il2cpp_property_get_name(prop);
        outPut << "\t";
        const Il2CppType *prop_type = nullptr;
        uint32_t iflags = 0;
        if (get) {
            outPut << get_method_modifier(il2cpp_method_get_flags(get, &iflags));
            prop_type = il2cpp_method_get_return_type(get);
        } else if (set) {
            outPut << get_method_modifier(il2cpp_method_get_flags(set, &iflags));
            auto param = il2cpp_method_get_param(set, 0);
            prop_type = param;
        }
        if (prop_type) {
            outPut << il2cpp_type_display_name(prop_type, false) << " " << prop_name << " { ";
            if (get) {
                outPut << "get; ";
            }
            if (set) {
                outPut << "set; ";
            }
            outPut << "}\n";
        } else {
            if (prop_name) {
                outPut << " // unknown property " << prop_name;
            }
        }
    }
    return outPut.str();
}

std::string dump_field(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Fields\n";
    void *iter = nullptr;
    while (auto field = il2cpp_class_get_fields(klass, &iter)) {
        //TODO attribute
        outPut << "\t";
        auto attrs = il2cpp_field_get_flags(field);
        auto access = attrs & FIELD_ATTRIBUTE_FIELD_ACCESS_MASK;
        switch (access) {
            case FIELD_ATTRIBUTE_PRIVATE:
                outPut << "private ";
                break;
            case FIELD_ATTRIBUTE_PUBLIC:
                outPut << "public ";
                break;
            case FIELD_ATTRIBUTE_FAMILY:
                outPut << "protected ";
                break;
            case FIELD_ATTRIBUTE_ASSEMBLY:
            case FIELD_ATTRIBUTE_FAM_AND_ASSEM:
                outPut << "internal ";
                break;
            case FIELD_ATTRIBUTE_FAM_OR_ASSEM:
                outPut << "protected internal ";
                break;
        }
        if (attrs & FIELD_ATTRIBUTE_LITERAL) {
            outPut << "const ";
        } else {
            if (attrs & FIELD_ATTRIBUTE_STATIC) {
                outPut << "static ";
            }
            if (attrs & FIELD_ATTRIBUTE_INIT_ONLY) {
                outPut << "readonly ";
            }
        }
        auto field_type = il2cpp_field_get_type(field);
        outPut << il2cpp_type_display_name(field_type, false) << " " << il2cpp_field_get_name(field);
        if (attrs & FIELD_ATTRIBUTE_LITERAL) {
            std::string literal;
            if (get_literal_value(field, field_type, literal)) outPut << " = " << literal;
        }
        outPut << ';';
        if (!(attrs & FIELD_ATTRIBUTE_LITERAL)) {
            outPut << " // 0x" << std::hex << il2cpp_field_get_offset(field);
        }
        outPut << "\n";
    }
    return outPut.str();
}

std::string dump_type(const Il2CppType *type) {
    std::stringstream outPut;
    auto *klass = il2cpp_class_from_type(type);
    outPut << "\n// Namespace: " << il2cpp_class_get_namespace(klass) << "\n";
    auto flags = il2cpp_class_get_flags(klass);
    if (flags & TYPE_ATTRIBUTE_SERIALIZABLE) {
        outPut << "[Serializable]\n";
    }
    //TODO attribute
    auto is_valuetype = il2cpp_class_is_valuetype(klass);
    auto is_enum = il2cpp_class_is_enum(klass);
    auto visibility = flags & TYPE_ATTRIBUTE_VISIBILITY_MASK;
    switch (visibility) {
        case TYPE_ATTRIBUTE_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_PUBLIC:
            outPut << "public ";
            break;
        case TYPE_ATTRIBUTE_NOT_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_FAM_AND_ASSEM:
        case TYPE_ATTRIBUTE_NESTED_ASSEMBLY:
            outPut << "internal ";
            break;
        case TYPE_ATTRIBUTE_NESTED_PRIVATE:
            outPut << "private ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAMILY:
            outPut << "protected ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAM_OR_ASSEM:
            outPut << "protected internal ";
            break;
    }
    if (flags & TYPE_ATTRIBUTE_ABSTRACT && flags & TYPE_ATTRIBUTE_SEALED) {
        outPut << "static ";
    } else if (!(flags & TYPE_ATTRIBUTE_INTERFACE) && flags & TYPE_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
    } else if (!is_valuetype && !is_enum && flags & TYPE_ATTRIBUTE_SEALED) {
        outPut << "sealed ";
    }
    if (flags & TYPE_ATTRIBUTE_INTERFACE) {
        outPut << "interface ";
    } else if (is_enum) {
        outPut << "enum ";
    } else if (is_valuetype) {
        outPut << "struct ";
    } else {
        outPut << "class ";
    }
    outPut << il2cpp_type_display_name(type, false); //TODO generic parameter declarations
    std::vector<std::string> extends;
    auto parent = il2cpp_class_get_parent(klass);
    if (!is_valuetype && !is_enum && parent) {
        auto parent_type = il2cpp_class_get_type(parent);
        if (parent_type->type != IL2CPP_TYPE_OBJECT) {
            extends.emplace_back(il2cpp_type_display_name(parent_type, false));
        }
    }
    void *iter = nullptr;
    while (auto itf = il2cpp_class_get_interfaces(klass, &iter)) {
        extends.emplace_back(il2cpp_type_display_name(il2cpp_class_get_type(itf), false));
    }
    if (!extends.empty()) {
        outPut << " : " << extends[0];
        for (int i = 1; i < extends.size(); ++i) {
            outPut << ", " << extends[i];
        }
    }
    outPut << "\n{";
    outPut << dump_field(klass);
    outPut << dump_property(klass);
    outPut << dump_method(klass);
    //TODO EventInfo
    outPut << "}\n";
    return outPut.str();
}

bool il2cpp_api_init(void *handle) {
    LOGI("il2cpp_handle: %p", handle);
    init_il2cpp_api(handle);

    if (!il2cpp_domain_get || !il2cpp_domain_get_assemblies || !il2cpp_assembly_get_image ||
        !il2cpp_image_get_name || !il2cpp_class_get_type || !il2cpp_class_from_type ||
        !il2cpp_class_get_name || !il2cpp_class_get_namespace || !il2cpp_class_get_flags ||
        !il2cpp_class_is_valuetype || !il2cpp_class_is_enum || !il2cpp_class_get_parent ||
        !il2cpp_class_get_interfaces || !il2cpp_class_get_fields || !il2cpp_class_get_properties ||
        !il2cpp_class_get_methods || !il2cpp_field_get_flags || !il2cpp_field_get_type ||
        !il2cpp_field_get_name || !il2cpp_field_get_offset || !il2cpp_property_get_get_method ||
        !il2cpp_property_get_set_method || !il2cpp_property_get_name || !il2cpp_method_get_flags ||
        !il2cpp_method_get_return_type || !il2cpp_method_get_name || !il2cpp_method_get_param_count ||
        !il2cpp_method_get_param || !il2cpp_method_get_param_name) {
        LOGE("Required il2cpp APIs are missing; dump aborted");
        return false;
    }

    Dl_info dlInfo;
    if (dladdr(reinterpret_cast<void *>(il2cpp_domain_get_assemblies), &dlInfo)) {
        il2cpp_base = reinterpret_cast<uint64_t>(dlInfo.dli_fbase);
    }
    if (!il2cpp_base) {
        LOGE("Failed to resolve libil2cpp.so image base");
        return false;
    }
    LOGI("il2cpp_base: %" PRIx64, il2cpp_base);

    while (il2cpp_is_vm_thread && !il2cpp_is_vm_thread(nullptr)) {
        LOGI("Waiting for il2cpp_init...");
        sleep(1);
    }
    auto domain = il2cpp_domain_get();
    while (!domain) {
        LOGI("Waiting for il2cpp domain...");
        sleep(1);
        domain = il2cpp_domain_get();
    }
    if (il2cpp_thread_attach) il2cpp_thread_attach(domain);
    return true;
}

void il2cpp_dump(const char *outDir) {
    LOGI("dumping...");
    size_t size = 0;
    auto domain = il2cpp_domain_get();
    auto assemblies = il2cpp_domain_get_assemblies(domain, &size);
    if (!assemblies) {
        LOGE("Failed to get il2cpp assemblies");
        return;
    }

    std::stringstream imageOutput;
    std::vector<std::string> imageNames;
    for (size_t i = 0; i < size; ++i) {
        auto image = il2cpp_assembly_get_image(assemblies[i]);
        const char *imageName = image ? il2cpp_image_get_name(image) : nullptr;
        imageOutput << "// Image " << i << ": " << (imageName ? imageName : "Unknown") << "\n";
        imageNames.emplace_back(imageName ? imageName : "Unknown");
    }

    const std::string filesDir = std::string(outDir) + "/files";
    const std::string outPath = filesDir + "/dump.cs";
    const std::string temporaryPath = outPath + ".tmp";
    std::ofstream outStream(temporaryPath, std::ios::out | std::ios::trunc);
    if (!outStream.is_open()) {
        LOGE("Failed to open %s", temporaryPath.c_str());
        return;
    }
    outStream << imageOutput.str();
    SemanticDumper semanticDumper;

    if (il2cpp_image_get_class && il2cpp_image_get_class_count) {
        LOGI("Version greater than 2018.3");
        for (size_t i = 0; i < size; ++i) {
            auto image = il2cpp_assembly_get_image(assemblies[i]);
            if (!image) continue;
            const char *imageName = il2cpp_image_get_name(image);
            outStream << "\n// Dll : " << (imageName ? imageName : "Unknown") << "\n";
            auto classCount = il2cpp_image_get_class_count(image);
            for (size_t j = 0; j < classCount; ++j) {
                auto klass = il2cpp_image_get_class(image, j);
                if (!klass) continue;
                semanticDumper.collect_class(imageName, const_cast<Il2CppClass *>(klass));
                auto type = il2cpp_class_get_type(const_cast<Il2CppClass *>(klass));
                if (type) outStream << dump_type(type);
            }
        }
    } else {
        LOGI("Version less than 2018.3");
        if (!il2cpp_get_corlib || !il2cpp_class_from_name || !il2cpp_class_get_method_from_name ||
            !il2cpp_string_new || !il2cpp_class_from_system_type) {
            LOGE("Reflection APIs required for this Unity version are missing");
            outStream.close();
            std::remove(temporaryPath.c_str());
            return;
        }
        auto corlib = il2cpp_get_corlib();
        auto assemblyClass = il2cpp_class_from_name(corlib, "System.Reflection", "Assembly");
        if (!assemblyClass) {
            LOGE("System.Reflection.Assembly not found");
            outStream.close();
            std::remove(temporaryPath.c_str());
            return;
        }
        auto assemblyLoad = il2cpp_class_get_method_from_name(assemblyClass, "Load", 1);
        auto assemblyGetTypes = il2cpp_class_get_method_from_name(assemblyClass, "GetTypes", 0);
        if (assemblyLoad && assemblyLoad->methodPointer) {
            LOGI("Assembly::Load: %p", assemblyLoad->methodPointer);
        } else {
            LOGI("miss Assembly::Load");
            outStream.close();
            std::remove(temporaryPath.c_str());
            return;
        }
        if (assemblyGetTypes && assemblyGetTypes->methodPointer) {
            LOGI("Assembly::GetTypes: %p", assemblyGetTypes->methodPointer);
        } else {
            LOGI("miss Assembly::GetTypes");
            outStream.close();
            std::remove(temporaryPath.c_str());
            return;
        }
        typedef void *(*Assembly_Load_ftn)(void *, Il2CppString *, void *);
        typedef Il2CppArray *(*Assembly_GetTypes_ftn)(void *, void *);
        for (size_t i = 0; i < size; ++i) {
            auto image = il2cpp_assembly_get_image(assemblies[i]);
            if (!image) continue;
            auto image_name = il2cpp_image_get_name(image);
            if (!image_name) image_name = "Unknown";
            outStream << "\n// Dll : " << image_name << "\n";
            auto imageName = std::string(image_name);
            auto pos = imageName.rfind('.');
            auto imageNameNoExt = imageName.substr(0, pos);
            auto assemblyFileName = il2cpp_string_new(imageNameNoExt.data());
            auto reflectionAssembly = ((Assembly_Load_ftn) assemblyLoad->methodPointer)(nullptr,
                                                                                        assemblyFileName,
                                                                                        nullptr);
            if (!reflectionAssembly) {
                LOGW("Failed to load reflection assembly %s", imageNameNoExt.c_str());
                continue;
            }
            auto reflectionTypes = ((Assembly_GetTypes_ftn) assemblyGetTypes->methodPointer)(
                    reflectionAssembly, nullptr);
            if (!reflectionTypes) continue;
            auto items = reflectionTypes->vector;
            for (size_t j = 0; j < reflectionTypes->max_length; ++j) {
                auto klass = il2cpp_class_from_system_type((Il2CppReflectionType *) items[j]);
                if (!klass) continue;
                semanticDumper.collect_class(image_name, klass);
                auto type = il2cpp_class_get_type(klass);
                if (type) outStream << dump_type(type);
            }
        }
    }
    outStream.close();
    if (!outStream || std::rename(temporaryPath.c_str(), outPath.c_str()) != 0) {
        LOGE("Failed to write %s", outPath.c_str());
        std::remove(temporaryPath.c_str());
        return;
    }
    LOGI("dump.cs written");

    semanticDumper.collect_registration(imageNames);
    semanticDumper.write_managed_json(filesDir + "/managed.json");
    semanticDumper.write_native_json(filesDir + "/native.json");
}
