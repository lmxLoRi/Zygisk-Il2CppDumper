#ifndef ZYGISK_IL2CPPDUMPER_REGISTRATION_DUMP_H
#define ZYGISK_IL2CPPDUMPER_REGISTRATION_DUMP_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct RegistrationMethodPointer {
    uint64_t address;
    std::string source;
};

struct CodeGenModuleEntry {
    std::string name;
    uint64_t address;
    uint64_t method_pointer_count;
    uint64_t method_pointers_address;
};

struct GenericMethodInstanceEntry {
    int32_t method_spec_index = -1;
    int32_t method_definition_index = -1;
    int32_t method_pointer_index = -1;
    uint64_t native_address = 0;
    std::vector<uint64_t> class_type_arguments;
    std::vector<uint64_t> method_type_arguments;
};

struct MetadataTargetEntry {
    uint64_t address;
    std::string kind;
    std::string id;
    std::string name;
};

struct MetadataSlotEntry {
    uint64_t address;
    uint64_t target_address;
    std::string target_kind;
    std::string target_id;
    std::string target_name;
};

struct RuntimeRegistrationInfo {
    bool metadata_registration_found = false;
    bool codegen_modules_found = false;
    bool generic_method_pointers_found = false;
    uint64_t metadata_registration_address = 0;
    uint64_t codegen_modules_field_address = 0;
    uint64_t generic_method_pointers_field_address = 0;
    uint64_t generic_method_pointer_count = 0;
    uint64_t generic_method_table_count = 0;
    uint64_t method_specs_count = 0;
    uint64_t codegen_count_matches = 0;
    uint64_t codegen_array_matches = 0;
    uint64_t codegen_max_valid_modules = 0;
    uint64_t generic_table_stride = 0;
    std::string status;
    std::vector<CodeGenModuleEntry> modules;
    std::vector<GenericMethodInstanceEntry> generic_instances;
    std::vector<MetadataSlotEntry> metadata_slots;
    std::vector<RegistrationMethodPointer> method_pointers;
};

class RuntimeRegistrationScanner {
public:
    RuntimeRegistrationInfo scan(size_t image_count, size_t type_definition_count,
                                 const std::vector<MetadataTargetEntry> &metadata_targets) const;
};

#endif
