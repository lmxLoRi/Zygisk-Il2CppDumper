#ifndef ZYGISK_IL2CPPDUMPER_SEMANTIC_DUMP_H
#define ZYGISK_IL2CPPDUMPER_SEMANTIC_DUMP_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "registration_dump.h"

struct Il2CppClass;
struct Il2CppType;

std::string il2cpp_type_display_name(const Il2CppType *type, bool qualify = true);

struct ManagedParameterEntry {
    uint32_t position;
    std::string name;
    std::string type;
    std::string modifier;
};

struct ManagedMethodEntry {
    std::string id;
    std::string assembly;
    std::string namespaze;
    std::string declaring_type;
    std::string method;
    std::string return_type;
    std::string signature;
    std::string native_function_id;
    std::string binding_kind;
    int64_t method_definition_index = -1;
    uint32_t token;
    uint32_t flags;
    bool is_static;
    bool is_abstract;
    bool is_virtual;
    bool has_observed_method_pointer;
    bool has_native_implementation;
    std::vector<ManagedParameterEntry> parameters;
};

struct NativeFunctionEntry {
    std::string id;
    uint64_t rva;
    uint64_t observed_va;
    std::vector<std::string> managed_method_ids;
    std::vector<std::string> sources;
};

class SemanticDumper {
public:
    void collect_class(const char *assembly_name, Il2CppClass *klass);
    void collect_registration(const std::vector<std::string> &image_names);
    bool write_managed_json(const std::string &out_path) const;
    bool write_native_json(const std::string &out_path) const;

private:
    std::string make_method_id(const std::string &assembly, uint32_t token,
                               const std::string &signature);

    std::vector<ManagedMethodEntry> methods_;
    std::map<uint64_t, NativeFunctionEntry> native_functions_;
    std::map<std::string, uint32_t> method_id_counts_;
    RuntimeRegistrationInfo registration_;
    std::vector<MetadataTargetEntry> metadata_targets_;
    size_t type_count_ = 0;
};

#endif
