#include "semantic_dump.h"

#include "il2cpp-class.h"
#include "il2cpp-tabledefs.h"
#include "il2cpp_dump.h"
#include "log.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

#define DO_API(r, n, p) extern r (*n) p
#include "il2cpp-api-functions.h"
#undef DO_API

namespace {

std::string escape_json(const std::string &value) {
    std::ostringstream output;
    for (unsigned char c : value) {
        switch (c) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (c < 0x20) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(c) << std::dec;
                } else {
                    output << c;
                }
        }
    }
    return output.str();
}

void write_json_string(std::ostream &output, const char *key, const std::string &value,
                       bool comma = true, int indent = 6) {
    output << std::string(indent, ' ') << '"' << key << "\": \"" << escape_json(value) << '"';
    if (comma) output << ',';
    output << '\n';
}

std::string strip_image_extension(const char *image_name) {
    std::string result = image_name ? image_name : "Unknown";
    const auto extension = result.rfind('.');
    if (extension != std::string::npos) result.resize(extension);
    return result;
}

std::string parameter_modifier(const Il2CppType *type) {
    if (!type || !(il2cpp_type_is_byref ? il2cpp_type_is_byref(type) : type->byref)) return "";
    if ((type->attrs & PARAM_ATTRIBUTE_OUT) && !(type->attrs & PARAM_ATTRIBUTE_IN)) return "out";
    if ((type->attrs & PARAM_ATTRIBUTE_IN) && !(type->attrs & PARAM_ATTRIBUTE_OUT)) return "in";
    return "ref";
}

std::string hex_value(uint64_t value, unsigned width = 0) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    if (width) output << std::setw(width);
    output << value;
    return output.str();
}

std::string unqualify_top_level_type(std::string value) {
    const auto generic_start = value.find('<');
    const auto search_end = generic_start == std::string::npos ? value.size() : generic_start;
    const auto separator = value.rfind('.', search_end);
    if (separator != std::string::npos) value.erase(0, separator + 1);
    return value;
}

bool finish_atomic_json(std::ofstream &output, const std::string &temporary_path,
                        const std::string &out_path) {
    output.close();
    if (!output) {
        LOGE("Failed to write %s", temporary_path.c_str());
        std::remove(temporary_path.c_str());
        return false;
    }
    if (std::rename(temporary_path.c_str(), out_path.c_str()) != 0) {
        LOGE("Failed to replace %s", out_path.c_str());
        std::remove(temporary_path.c_str());
        return false;
    }
    return true;
}

} // namespace

std::string il2cpp_type_display_name(const Il2CppType *type, bool qualify) {
    if (!type) return "?";

    if (il2cpp_type_get_name && il2cpp_free) {
        char *raw_name = il2cpp_type_get_name(type);
        if (raw_name) {
            std::string result(raw_name);
            il2cpp_free(raw_name);
            if (!result.empty() && result.back() == '&') result.pop_back();
            if (!qualify) result = unqualify_top_level_type(std::move(result));
            return result;
        }
    }

    auto klass = il2cpp_class_from_type(type);
    if (!klass) return "?";
    const char *name = il2cpp_class_get_name(klass);
    const char *namespaze = il2cpp_class_get_namespace(klass);
    if (!name) return "?";
    if (qualify && namespaze && namespaze[0]) return std::string(namespaze) + "." + name;
    return name;
}

std::string SemanticDumper::make_method_id(const std::string &assembly, uint32_t token,
                                           const std::string &signature) {
    std::string base;
    if (token) {
        base = "managed:" + assembly + ":0x" + hex_value(token, 8);
    } else {
        base = "managed:" + assembly + ":signature:" + signature;
    }
    const uint32_t duplicate = method_id_counts_[base]++;
    return duplicate ? base + ":duplicate:" + std::to_string(duplicate) : base;
}

void SemanticDumper::collect_class(const char *assembly_name, Il2CppClass *klass) {
    if (!klass) return;
    ++type_count_;

    const std::string assembly = strip_image_extension(assembly_name);
    const char *raw_namespace = il2cpp_class_get_namespace(klass);
    const std::string namespaze = raw_namespace ? raw_namespace : "";
    const Il2CppType *class_type = il2cpp_class_get_type(klass);
    const std::string declaring_type = il2cpp_type_display_name(class_type);
    const bool is_interface = (il2cpp_class_get_flags(klass) & TYPE_ATTRIBUTE_INTERFACE) != 0;
    const std::string type_id = "managed-type:" + assembly + ":" + declaring_type;
    metadata_targets_.push_back({reinterpret_cast<uint64_t>(klass), "type_info", type_id,
                                 declaring_type});
    if (class_type) {
        metadata_targets_.push_back({reinterpret_cast<uint64_t>(class_type), "type_ref", type_id,
                                     declaring_type});
    }

    void *iterator = nullptr;
    while (auto method_info = il2cpp_class_get_methods(klass, &iterator)) {
        ManagedMethodEntry entry{};
        entry.assembly = assembly;
        entry.namespaze = namespaze;
        entry.declaring_type = declaring_type;
        entry.token = il2cpp_method_get_token ? il2cpp_method_get_token(method_info) : 0;

        uint32_t implementation_flags = 0;
        entry.flags = il2cpp_method_get_flags(method_info, &implementation_flags);
        entry.is_static = (entry.flags & METHOD_ATTRIBUTE_STATIC) != 0;
        entry.is_abstract = (entry.flags & METHOD_ATTRIBUTE_ABSTRACT) != 0;
        entry.is_virtual = (entry.flags & METHOD_ATTRIBUTE_VIRTUAL) != 0;
        entry.has_observed_method_pointer = method_info->methodPointer != nullptr;
        entry.has_native_implementation = entry.has_observed_method_pointer &&
                                          !entry.is_abstract && !is_interface;

        const char *raw_method = il2cpp_method_get_name(method_info);
        entry.method = raw_method ? raw_method : "?";
        entry.return_type = il2cpp_type_display_name(il2cpp_method_get_return_type(method_info));

        const uint32_t parameter_count = il2cpp_method_get_param_count(method_info);
        for (uint32_t index = 0; index < parameter_count; ++index) {
            const Il2CppType *parameter_type = il2cpp_method_get_param(method_info, index);
            const char *raw_name = il2cpp_method_get_param_name(method_info, index);
            ManagedParameterEntry parameter{};
            parameter.position = index;
            parameter.name = raw_name && raw_name[0] ? raw_name : "arg" + std::to_string(index);
            parameter.type = il2cpp_type_display_name(parameter_type);
            parameter.modifier = parameter_modifier(parameter_type);
            entry.parameters.push_back(std::move(parameter));
        }

        std::ostringstream signature;
        signature << entry.return_type << ' ' << entry.declaring_type << "::" << entry.method << '(';
        for (size_t index = 0; index < entry.parameters.size(); ++index) {
            const auto &parameter = entry.parameters[index];
            if (!parameter.modifier.empty()) signature << parameter.modifier << ' ';
            signature << parameter.type << ' ' << parameter.name;
            if (index + 1 < entry.parameters.size()) signature << ", ";
        }
        signature << ')';
        entry.signature = signature.str();
        entry.id = make_method_id(assembly, entry.token, entry.signature);
        metadata_targets_.push_back({reinterpret_cast<uint64_t>(method_info), "method_info",
                                     entry.id, entry.signature});

        if (entry.has_observed_method_pointer) {
            const uint64_t observed_va = reinterpret_cast<uint64_t>(method_info->methodPointer);
            if (il2cpp_base && observed_va >= il2cpp_base) {
                const uint64_t rva = observed_va - il2cpp_base;
                auto [native, inserted] = native_functions_.try_emplace(rva);
                if (inserted) {
                    native->second.rva = rva;
                    native->second.observed_va = observed_va;
                    native->second.id = "native:rva:0x" + hex_value(rva);
                }
                if (std::find(native->second.sources.begin(), native->second.sources.end(),
                              "runtime_method_pointer") == native->second.sources.end()) {
                    native->second.sources.emplace_back("runtime_method_pointer");
                }
                entry.native_function_id = native->second.id;
                entry.binding_kind = entry.has_native_implementation
                        ? "runtime_method_pointer"
                        : "runtime_stub_candidate";
                native->second.managed_method_ids.push_back(entry.id);
            }
        }
        methods_.push_back(std::move(entry));
    }

    void *field_iterator = nullptr;
    while (auto field = il2cpp_class_get_fields(klass, &field_iterator)) {
        const char *raw_field_name = il2cpp_field_get_name(field);
        const std::string field_name = raw_field_name ? raw_field_name : "?";
        const std::string qualified_name = declaring_type + "::" + field_name;
        metadata_targets_.push_back({reinterpret_cast<uint64_t>(field), "field_info",
                                     "managed-field:" + assembly + ":" + qualified_name,
                                     qualified_name});
    }
}

void SemanticDumper::collect_registration(const std::vector<std::string> &image_names) {
    RuntimeRegistrationScanner scanner;
    registration_ = scanner.scan(image_names.size(), type_count_, metadata_targets_);

    std::map<std::string, uint64_t> module_method_counts;
    for (const auto &module : registration_.modules) {
        module_method_counts[strip_image_extension(module.name.c_str())] = module.method_pointer_count;
    }
    std::map<std::string, uint64_t> method_definition_starts;
    uint64_t method_definition_start = 0;
    for (const auto &image_name : image_names) {
        const std::string assembly = strip_image_extension(image_name.c_str());
        method_definition_starts[assembly] = method_definition_start;
        const auto count = module_method_counts.find(assembly);
        if (count != module_method_counts.end()) method_definition_start += count->second;
    }
    for (auto &method : methods_) {
        const auto start = method_definition_starts.find(method.assembly);
        const uint32_t row = method.token & 0x00ffffff;
        const auto count = module_method_counts.find(method.assembly);
        if (start != method_definition_starts.end() && count != module_method_counts.end() &&
            row > 0 && row <= count->second) {
            method.method_definition_index = static_cast<int64_t>(start->second + row - 1);
        }
    }
    for (const auto &pointer : registration_.method_pointers) {
        const uint64_t observed_va = pointer.address;
        if (!il2cpp_base || observed_va < il2cpp_base) continue;
        const uint64_t rva = observed_va - il2cpp_base;
        auto [native, inserted] = native_functions_.try_emplace(rva);
        if (inserted) {
            native->second.rva = rva;
            native->second.observed_va = observed_va;
            native->second.id = "native:rva:0x" + hex_value(rva);
        }
        if (std::find(native->second.sources.begin(), native->second.sources.end(), pointer.source) ==
            native->second.sources.end()) {
            native->second.sources.push_back(pointer.source);
        }
    }
}

bool SemanticDumper::write_managed_json(const std::string &out_path) const {
    const std::string temporary_path = out_path + ".tmp";
    std::ofstream output(temporary_path, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        LOGE("Failed to open %s", temporary_path.c_str());
        return false;
    }

    size_t bound_count = 0;
    for (const auto &method : methods_) if (!method.native_function_id.empty()) ++bound_count;

    output << "{\n"
           << "  \"SchemaVersion\": 2,\n"
           << "  \"Kind\": \"IL2CPP managed runtime metadata\",\n"
           << "  \"MethodCount\": " << methods_.size() << ",\n"
           << "  \"NativeBindingCount\": " << bound_count << ",\n"
           << "  \"ManagedMethods\": [\n";

    for (size_t index = 0; index < methods_.size(); ++index) {
        const auto &method = methods_[index];
        output << "    {\n";
        write_json_string(output, "Id", method.id);
        write_json_string(output, "Assembly", method.assembly);
        write_json_string(output, "Namespace", method.namespaze);
        write_json_string(output, "DeclaringType", method.declaring_type);
        write_json_string(output, "Method", method.method);
        output << "      \"Token\": " << method.token << ",\n"
               << "      \"MethodDefinitionIndex\": ";
        if (method.method_definition_index >= 0) output << method.method_definition_index;
        else output << "null";
        output << ",\n"
               << "      \"Flags\": " << method.flags << ",\n"
               << "      \"IsStatic\": " << (method.is_static ? "true" : "false") << ",\n"
               << "      \"IsAbstract\": " << (method.is_abstract ? "true" : "false") << ",\n"
               << "      \"IsVirtual\": " << (method.is_virtual ? "true" : "false") << ",\n"
               << "      \"HasObservedMethodPointer\": "
               << (method.has_observed_method_pointer ? "true" : "false") << ",\n"
               << "      \"HasNativeImplementation\": "
               << (method.has_native_implementation ? "true" : "false") << ",\n";
        write_json_string(output, "ReturnType", method.return_type);
        write_json_string(output, "ManagedSignature", method.signature);
        if (method.native_function_id.empty()) {
            output << "      \"NativeFunctionId\": null,\n";
            output << "      \"BindingKind\": null,\n";
        } else {
            write_json_string(output, "NativeFunctionId", method.native_function_id);
            write_json_string(output, "BindingKind", method.binding_kind);
        }
        output << "      \"Parameters\": [\n";
        for (size_t parameter_index = 0; parameter_index < method.parameters.size(); ++parameter_index) {
            const auto &parameter = method.parameters[parameter_index];
            output << "        {\n"
                   << "          \"Position\": " << parameter.position << ",\n";
            write_json_string(output, "Name", parameter.name, true, 10);
            write_json_string(output, "Type", parameter.type, true, 10);
            write_json_string(output, "Modifier", parameter.modifier, false, 10);
            output << "        }" << (parameter_index + 1 < method.parameters.size() ? "," : "") << '\n';
        }
        output << "      ],\n"
               << "      \"Source\": \"exported_api\",\n"
               << "      \"Confidence\": \"high\"\n"
               << "    }" << (index + 1 < methods_.size() ? "," : "") << '\n';
    }
    output << "  ]\n}\n";

    if (!finish_atomic_json(output, temporary_path, out_path)) return false;
    LOGI("managed.json written with %zu methods", methods_.size());
    return true;
}

bool SemanticDumper::write_native_json(const std::string &out_path) const {
    const std::string temporary_path = out_path + ".tmp";
    std::ofstream output(temporary_path, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        LOGE("Failed to open %s", temporary_path.c_str());
        return false;
    }

    size_t metadata_slot_count = 0;
    size_t pointer_reference_count = 0;
    for (const auto &slot : registration_.metadata_slots) {
        if (slot.target_id.rfind("metadata-type:", 0) == 0) ++pointer_reference_count;
        else ++metadata_slot_count;
    }

    output << "{\n"
           << "  \"SchemaVersion\": 2,\n"
           << "  \"Kind\": \"IL2CPP native runtime index\",\n"
           << "  \"Image\": {\n"
           << "    \"Module\": \"libil2cpp.so\",\n"
           << "    \"ObservedImageBase\": " << il2cpp_base << ",\n"
           << "    \"StableAddressKind\": \"RVA\"\n"
           << "  },\n"
           << "  \"Capabilities\": {\n"
           << "    \"RuntimeMethodPointers\": true,\n"
           << "    \"RegistrationMetadata\": "
           << (registration_.metadata_registration_found ? "true" : "false") << ",\n"
           << "    \"CodeGenModules\": "
           << (registration_.codegen_modules_found ? "true" : "false") << ",\n"
           << "    \"GenericMethodPointers\": "
           << (registration_.generic_method_pointers_found ? "true" : "false") << ",\n"
           << "    \"NativeSymbols\": false,\n"
           << "    \"GenericInstances\": "
           << (!registration_.generic_instances.empty() ? "true" : "false") << ",\n"
           << "    \"StringLiterals\": false,\n"
           << "    \"RuntimeMetadataSlots\": " << (metadata_slot_count ? "true" : "false") << ",\n"
           << "    \"PointerReferences\": " << (pointer_reference_count ? "true" : "false") << ",\n"
           << "    \"StaticMetadataUsageTable\": false\n"
           << "  },\n"
           << "  \"Registration\": {\n";
    write_json_string(output, "Status", registration_.status, true, 4);
    output << "    \"MetadataRegistrationRVA\": ";
    if (registration_.metadata_registration_address >= il2cpp_base) {
        output << registration_.metadata_registration_address - il2cpp_base;
    } else {
        output << "null";
    }
    output << ",\n    \"CodeGenModulesFieldRVA\": ";
    if (registration_.codegen_modules_field_address >= il2cpp_base) {
        output << registration_.codegen_modules_field_address - il2cpp_base;
    } else {
        output << "null";
    }
    output << ",\n    \"GenericMethodPointersFieldRVA\": ";
    if (registration_.generic_method_pointers_field_address >= il2cpp_base) {
        output << registration_.generic_method_pointers_field_address - il2cpp_base;
    } else {
        output << "null";
    }
    output << ",\n"
           << "    \"GenericMethodPointerCount\": " << registration_.generic_method_pointer_count << ",\n"
           << "    \"GenericMethodTableCount\": " << registration_.generic_method_table_count << ",\n"
           << "    \"MethodSpecsCount\": " << registration_.method_specs_count << ",\n"
           << "    \"DecodedGenericInstanceCount\": " << registration_.generic_instances.size() << ",\n"
           << "    \"MetadataSlotCount\": " << metadata_slot_count << ",\n"
           << "    \"PointerReferenceCount\": " << pointer_reference_count << ",\n"
           << "    \"Diagnostics\": {\n"
           << "      \"CodeGenCountMatches\": " << registration_.codegen_count_matches << ",\n"
           << "      \"CodeGenArrayMatches\": " << registration_.codegen_array_matches << ",\n"
           << "      \"CodeGenMaxValidModules\": " << registration_.codegen_max_valid_modules << ",\n"
           << "      \"GenericTableStride\": " << registration_.generic_table_stride << "\n"
           << "    },\n"
           << "    \"CodeGenModules\": [\n";
    for (size_t module_index = 0; module_index < registration_.modules.size(); ++module_index) {
        const auto &module = registration_.modules[module_index];
        output << "      {\n";
        write_json_string(output, "Name", module.name, true, 8);
        output << "        \"RVA\": "
               << (module.address >= il2cpp_base ? module.address - il2cpp_base : 0) << ",\n"
               << "        \"MethodPointerCount\": " << module.method_pointer_count << "\n"
               << "      }" << (module_index + 1 < registration_.modules.size() ? "," : "") << '\n';
    }
    output << "    ]\n"
           << "  },\n"
           << "  \"NativeFunctionCount\": " << native_functions_.size() << ",\n"
           << "  \"NativeFunctions\": [\n";

    size_t index = 0;
    for (const auto &[rva, function] : native_functions_) {
        output << "    {\n";
        write_json_string(output, "Id", function.id);
        output << "      \"RVA\": " << rva << ",\n"
               << "      \"ObservedVA\": " << function.observed_va << ",\n"
               << "      \"IsShared\": " << (function.managed_method_ids.size() > 1 ? "true" : "false") << ",\n"
               << "      \"ManagedMethodIds\": [\n";
        for (size_t method_index = 0; method_index < function.managed_method_ids.size(); ++method_index) {
            output << "        \"" << escape_json(function.managed_method_ids[method_index]) << "\""
                   << (method_index + 1 < function.managed_method_ids.size() ? "," : "") << '\n';
        }
        output << "      ],\n"
               << "      \"Sources\": [";
        for (size_t source_index = 0; source_index < function.sources.size(); ++source_index) {
            output << "\"" << escape_json(function.sources[source_index]) << "\"";
            if (source_index + 1 < function.sources.size()) output << ',';
        }
        output << "],\n"
               << "      \"Confidence\": \"high\"\n"
               << "    }" << (++index < native_functions_.size() ? "," : "") << '\n';
    }
    output << "  ],\n"
           << "  \"GenericInstances\": [\n";
    std::map<int64_t, std::string> method_ids_by_definition;
    for (const auto &method : methods_) {
        if (method.method_definition_index >= 0) {
            method_ids_by_definition[method.method_definition_index] = method.id;
        }
    }
    for (size_t instance_index = 0; instance_index < registration_.generic_instances.size();
         ++instance_index) {
        const auto &instance = registration_.generic_instances[instance_index];
        output << "    {\n"
               << "      \"MethodSpecIndex\": " << instance.method_spec_index << ",\n"
               << "      \"MethodDefinitionIndex\": " << instance.method_definition_index << ",\n"
               << "      \"MethodPointerIndex\": " << instance.method_pointer_index << ",\n"
               << "      \"ManagedMethodId\": ";
        const auto managed = method_ids_by_definition.find(instance.method_definition_index);
        if (managed != method_ids_by_definition.end()) {
            output << "\"" << escape_json(managed->second) << "\"";
        } else {
            output << "null";
        }
        output << ",\n      \"NativeFunctionId\": ";
        if (instance.native_address >= il2cpp_base) {
            output << "\"native:rva:0x" << hex_value(instance.native_address - il2cpp_base) << "\"";
        } else {
            output << "null";
        }
        output << ",\n      \"ClassTypeArguments\": [";
        for (size_t argument_index = 0; argument_index < instance.class_type_arguments.size();
             ++argument_index) {
            const auto type = reinterpret_cast<const Il2CppType *>(
                    instance.class_type_arguments[argument_index]);
            output << "\"" << escape_json(il2cpp_type_display_name(type)) << "\"";
            if (argument_index + 1 < instance.class_type_arguments.size()) output << ',';
        }
        output << "],\n      \"MethodTypeArguments\": [";
        for (size_t argument_index = 0; argument_index < instance.method_type_arguments.size();
             ++argument_index) {
            const auto type = reinterpret_cast<const Il2CppType *>(
                    instance.method_type_arguments[argument_index]);
            output << "\"" << escape_json(il2cpp_type_display_name(type)) << "\"";
            if (argument_index + 1 < instance.method_type_arguments.size()) output << ',';
        }
        output << "],\n"
               << "      \"Source\": \"runtime_registration\",\n"
               << "      \"Confidence\": \"high\"\n"
               << "    }" << (instance_index + 1 < registration_.generic_instances.size() ? "," : "")
               << '\n';
    }
    output << "  ],\n"
           << "  \"StringLiterals\": [],\n"
           << "  \"MetadataSlots\": [\n";
    size_t written_metadata_slots = 0;
    for (const auto &slot : registration_.metadata_slots) {
        if (slot.target_id.rfind("metadata-type:", 0) == 0) continue;
        std::string target_name = slot.target_name;
        if (target_name.empty() && slot.target_kind == "type_ref") {
            target_name = il2cpp_type_display_name(
                    reinterpret_cast<const Il2CppType *>(slot.target_address));
        } else if (target_name.empty() && slot.target_kind == "type_info") {
            auto klass = reinterpret_cast<Il2CppClass *>(slot.target_address);
            target_name = il2cpp_type_display_name(il2cpp_class_get_type(klass));
        }
        output << "    {\n"
               << "      \"SlotRVA\": " << (slot.address - il2cpp_base) << ",\n"
               << "      \"ObservedTargetVA\": " << slot.target_address << ",\n";
        write_json_string(output, "TargetKind", slot.target_kind);
        write_json_string(output, "TargetId", slot.target_id);
        write_json_string(output, "TargetName", target_name);
        output << "      \"Source\": \"runtime_pointer_scan\",\n"
               << "      \"Confidence\": \"high\"\n"
               << "    }" << (++written_metadata_slots < metadata_slot_count ? "," : "")
               << '\n';
    }
    output << "  ],\n"
           << "  \"PointerReferences\": [\n";
    size_t written_pointer_references = 0;
    for (const auto &slot : registration_.metadata_slots) {
        if (slot.target_id.rfind("metadata-type:", 0) != 0) continue;
        std::string target_name;
        if (slot.target_kind == "type_ref") {
            target_name = il2cpp_type_display_name(
                    reinterpret_cast<const Il2CppType *>(slot.target_address));
        } else if (slot.target_kind == "type_info") {
            auto klass = reinterpret_cast<Il2CppClass *>(slot.target_address);
            target_name = il2cpp_type_display_name(il2cpp_class_get_type(klass));
        }
        output << "    {\n"
               << "      \"ReferenceRVA\": " << (slot.address - il2cpp_base) << ",\n"
               << "      \"ObservedTargetVA\": " << slot.target_address << ",\n";
        write_json_string(output, "TargetKind", slot.target_kind);
        write_json_string(output, "TargetId", slot.target_id);
        write_json_string(output, "TargetName", target_name);
        output << "      \"Source\": \"registration_pointer_scan\",\n"
               << "      \"Confidence\": \"high\"\n"
               << "    }" << (++written_pointer_references < pointer_reference_count ? "," : "")
               << '\n';
    }
    output << "  ],\n"
           << "  \"UnresolvedRecords\": []\n"
           << "}\n";

    if (!finish_atomic_json(output, temporary_path, out_path)) return false;
    LOGI("native.json written with %zu native functions", native_functions_.size());
    return true;
}
