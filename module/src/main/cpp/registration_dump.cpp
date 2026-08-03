#include "registration_dump.h"

#include "il2cpp-class.h"
#include "log.h"
#include "xdl.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <link.h>
#include <set>
#include <unordered_map>

#define DO_API(r, n, p) extern r (*n) p
#include "il2cpp-api-functions.h"
#undef DO_API

namespace {

constexpr uintptr_t kMaxRegistrationCount = 500000;

struct MemoryRange {
    uintptr_t begin;
    uintptr_t end;
    bool executable;
};

struct MetadataCandidate {
    uintptr_t address = 0;
    uintptr_t generic_insts_count = 0;
    uintptr_t generic_insts = 0;
    uintptr_t generic_method_table_count = 0;
    uintptr_t generic_method_table = 0;
    uintptr_t types_count = 0;
    uintptr_t types = 0;
    uintptr_t method_specs_count = 0;
    uintptr_t method_specs = 0;
};

struct CodeGenCandidate {
    uintptr_t field_address = 0;
    uintptr_t modules_address = 0;
    std::vector<CodeGenModuleEntry> modules;
};

bool multiply_fits(uintptr_t count, size_t size, size_t &result) {
    if (count > std::numeric_limits<size_t>::max() / size) return false;
    result = static_cast<size_t>(count) * size;
    return true;
}

class MemoryView {
public:
    bool initialize() {
        if (!il2cpp_domain_get_assemblies) return false;
        xdl_info_t info{};
        void *cache = nullptr;
        if (xdl_addr(reinterpret_cast<void *>(il2cpp_domain_get_assemblies), &info, &cache) == 0) {
            xdl_addr_clean(&cache);
            return false;
        }
        base_ = reinterpret_cast<uintptr_t>(info.dli_fbase);
        for (size_t index = 0; index < info.dlpi_phnum; ++index) {
            const auto &header = info.dlpi_phdr[index];
            if (header.p_type != PT_LOAD || !(header.p_flags & PF_R) || !header.p_memsz) continue;
            const uintptr_t begin = base_ + header.p_vaddr;
            if (header.p_memsz > std::numeric_limits<uintptr_t>::max() - begin) continue;
            ranges_.push_back({begin, begin + header.p_memsz, (header.p_flags & PF_X) != 0});
        }
        xdl_addr_clean(&cache);
        return base_ && !ranges_.empty();
    }

    uintptr_t base() const { return base_; }
    const std::vector<MemoryRange> &ranges() const { return ranges_; }

    bool contains(uintptr_t address, size_t size, bool executable = false) const {
        if (!address || size > std::numeric_limits<uintptr_t>::max() - address) return false;
        const uintptr_t end = address + size;
        for (const auto &range : ranges_) {
            if (address >= range.begin && end <= range.end && (!executable || range.executable)) return true;
        }
        return false;
    }

    bool read_word(uintptr_t address, uintptr_t &value) const {
        if (!contains(address, sizeof(value))) return false;
        std::memcpy(&value, reinterpret_cast<const void *>(address), sizeof(value));
        return true;
    }

    bool read_i32(uintptr_t address, int32_t &value) const {
        if (!contains(address, sizeof(value))) return false;
        std::memcpy(&value, reinterpret_cast<const void *>(address), sizeof(value));
        return true;
    }

    bool read_string(uintptr_t address, std::string &value) const {
        if (!address) return false;
        const MemoryRange *owner = nullptr;
        for (const auto &range : ranges_) {
            if (address >= range.begin && address < range.end) {
                owner = &range;
                break;
            }
        }
        if (!owner) return false;
        const size_t limit = std::min<size_t>(256, owner->end - address);
        const char *text = reinterpret_cast<const char *>(address);
        size_t length = 0;
        while (length < limit && text[length]) {
            const unsigned char c = text[length];
            if (c < 0x20 || c > 0x7e) return false;
            ++length;
        }
        if (!length || length == limit) return false;
        value.assign(text, length);
        return true;
    }

private:
    uintptr_t base_ = 0;
    std::vector<MemoryRange> ranges_;
};

bool valid_count(uintptr_t count) {
    return count > 0 && count <= kMaxRegistrationCount;
}

bool validate_pointer_array(const MemoryView &memory, uintptr_t address, uintptr_t count,
                            bool targets_executable) {
    if (!valid_count(count)) return false;
    size_t bytes = 0;
    if (!multiply_fits(count, sizeof(uintptr_t), bytes) || !memory.contains(address, bytes)) return false;

    const uintptr_t samples = std::min<uintptr_t>(count, 64);
    uintptr_t nonzero = 0;
    uintptr_t valid = 0;
    for (uintptr_t index = 0; index < samples; ++index) {
        const uintptr_t sample_index = samples == count ? index : index * (count - 1) / (samples - 1);
        uintptr_t pointer = 0;
        if (!memory.read_word(address + sample_index * sizeof(uintptr_t), pointer)) return false;
        if (!pointer) continue;
        ++nonzero;
        if (memory.contains(pointer, 1, targets_executable)) ++valid;
    }
    return nonzero > 0 && valid * 10 >= nonzero * 9;
}

bool read_module(const MemoryView &memory, uintptr_t address, CodeGenModuleEntry &module) {
    uintptr_t name_pointer = 0;
    uintptr_t method_count = 0;
    uintptr_t method_pointers = 0;
    if (!memory.read_word(address, name_pointer) ||
        !memory.read_word(address + sizeof(uintptr_t), method_count) ||
        !memory.read_word(address + 2 * sizeof(uintptr_t), method_pointers)) {
        return false;
    }
    if (method_count > kMaxRegistrationCount) return false;
    std::string name;
    if (!memory.read_string(name_pointer, name)) return false;
    if (method_count && !validate_pointer_array(memory, method_pointers, method_count, true)) return false;
    module = {name, address, method_count, method_pointers};
    return true;
}

CodeGenCandidate find_codegen_modules(const MemoryView &memory, size_t image_count,
                                      RuntimeRegistrationInfo &diagnostics) {
    CodeGenCandidate best;
    if (!image_count || image_count > kMaxRegistrationCount) return best;
    const uintptr_t minimum_count = image_count > 8 ? image_count - 8 : 1;
    const uintptr_t maximum_count = image_count + 16;

    for (const auto &range : memory.ranges()) {
        if (range.end - range.begin < 2 * sizeof(uintptr_t)) continue;
        uintptr_t cursor = (range.begin + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);
        for (; cursor + 2 * sizeof(uintptr_t) <= range.end; cursor += sizeof(uintptr_t)) {
            uintptr_t count = 0;
            uintptr_t modules_pointer = 0;
            if (!memory.read_word(cursor, count) || count < minimum_count || count > maximum_count ||
                !memory.read_word(cursor + sizeof(uintptr_t), modules_pointer)) continue;
            ++diagnostics.codegen_count_matches;

            size_t array_size = 0;
            if (!multiply_fits(count, sizeof(uintptr_t), array_size) ||
                !memory.contains(modules_pointer, array_size)) continue;
            ++diagnostics.codegen_array_matches;

            CodeGenCandidate candidate;
            candidate.field_address = cursor;
            candidate.modules_address = modules_pointer;
            std::set<std::string> names;
            bool valid = true;
            size_t dll_names = 0;
            for (uintptr_t index = 0; index < count; ++index) {
                uintptr_t module_pointer = 0;
                CodeGenModuleEntry module;
                if (!memory.read_word(modules_pointer + index * sizeof(uintptr_t), module_pointer) ||
                    !read_module(memory, module_pointer, module) || !names.insert(module.name).second) {
                    valid = false;
                    break;
                }
                if (module.name.size() >= 4 && module.name.substr(module.name.size() - 4) == ".dll") {
                    ++dll_names;
                }
                candidate.modules.push_back(std::move(module));
            }
            diagnostics.codegen_max_valid_modules = std::max<uint64_t>(
                    diagnostics.codegen_max_valid_modules, candidate.modules.size());
            if (valid && dll_names * 10 >= count * 8 && candidate.modules.size() > best.modules.size()) {
                best = std::move(candidate);
            }
        }
    }
    return best;
}

MetadataCandidate find_metadata_registration(const MemoryView &memory, size_t type_count) {
    MetadataCandidate result;
    if (!type_count || type_count > kMaxRegistrationCount) return result;

    constexpr size_t fields = 16;
    for (const auto &range : memory.ranges()) {
        if (range.executable || range.end - range.begin < fields * sizeof(uintptr_t)) continue;
        uintptr_t cursor = (range.begin + 10 * sizeof(uintptr_t) + sizeof(uintptr_t) - 1) &
                           ~(sizeof(uintptr_t) - 1);
        for (; cursor + 6 * sizeof(uintptr_t) <= range.end; cursor += sizeof(uintptr_t)) {
            uintptr_t first_count = 0;
            uintptr_t second_count = 0;
            if (!memory.read_word(cursor, first_count) || first_count != type_count ||
                !memory.read_word(cursor + 2 * sizeof(uintptr_t), second_count) ||
                second_count != type_count) continue;

            const uintptr_t start = cursor - 10 * sizeof(uintptr_t);
            if (!memory.contains(start, fields * sizeof(uintptr_t))) continue;
            uintptr_t words[fields]{};
            bool readable = true;
            for (size_t index = 0; index < fields; ++index) {
                if (!memory.read_word(start + index * sizeof(uintptr_t), words[index])) {
                    readable = false;
                    break;
                }
            }
            if (!readable || !valid_count(words[1 * 2]) || !valid_count(words[2 * 2]) ||
                !valid_count(words[3 * 2]) || !valid_count(words[4 * 2])) continue;
            if (!validate_pointer_array(memory, words[3 * 2 + 1], words[3 * 2], false)) continue;
            size_t table_bytes = 0;
            if (!multiply_fits(words[2 * 2], 12, table_bytes) ||
                !memory.contains(words[2 * 2 + 1], table_bytes)) continue;
            if (!multiply_fits(words[4 * 2], 12, table_bytes) ||
                !memory.contains(words[4 * 2 + 1], table_bytes)) continue;

            result.address = start;
            result.generic_insts_count = words[2];
            result.generic_insts = words[3];
            result.generic_method_table_count = words[4];
            result.generic_method_table = words[5];
            result.types_count = words[6];
            result.types = words[7];
            result.method_specs_count = words[8];
            result.method_specs = words[9];
            return result;
        }
    }
    return result;
}

size_t detect_generic_table_stride(const MemoryView &memory, const MetadataCandidate &metadata,
                                   int32_t &max_method_index) {
    size_t best_stride = 0;
    size_t best_valid = 0;
    max_method_index = -1;
    for (const size_t stride : {size_t{12}, size_t{16}}) {
        size_t bytes = 0;
        if (!multiply_fits(metadata.generic_method_table_count, stride, bytes) ||
            !memory.contains(metadata.generic_method_table, bytes)) continue;
        size_t valid = 0;
        int32_t local_max = -1;
        for (uintptr_t index = 0; index < metadata.generic_method_table_count; ++index) {
            const uintptr_t entry = metadata.generic_method_table + index * stride;
            int32_t generic_index = -1;
            int32_t method_index = -1;
            int32_t invoker_index = -1;
            if (!memory.read_i32(entry, generic_index) ||
                !memory.read_i32(entry + 4, method_index) ||
                !memory.read_i32(entry + 8, invoker_index)) continue;
            if (generic_index < 0 || static_cast<uintptr_t>(generic_index) >= metadata.method_specs_count ||
                method_index < -1 || method_index >= static_cast<int32_t>(kMaxRegistrationCount) ||
                invoker_index < -1 || invoker_index >= static_cast<int32_t>(kMaxRegistrationCount)) continue;
            ++valid;
            local_max = std::max(local_max, method_index);
        }
        if (valid > best_valid) {
            best_valid = valid;
            best_stride = stride;
            max_method_index = local_max;
        }
    }
    if (!best_stride || best_valid * 100 < metadata.generic_method_table_count * 99) return 0;
    return best_stride;
}

bool find_generic_method_pointers(const MemoryView &memory, const CodeGenCandidate &codegen,
                                  int32_t max_method_index, uintptr_t &field_address,
                                  uintptr_t &count, uintptr_t &pointers) {
    if (!codegen.field_address || max_method_index < 0) return false;
    const uintptr_t minimum_count = static_cast<uintptr_t>(max_method_index) + 1;
    uintptr_t best_count = std::numeric_limits<uintptr_t>::max();
    for (size_t back = 2; back <= 24; ++back) {
        if (codegen.field_address < back * sizeof(uintptr_t)) break;
        const uintptr_t candidate_field = codegen.field_address - back * sizeof(uintptr_t);
        uintptr_t candidate_count = 0;
        uintptr_t candidate_pointers = 0;
        if (!memory.read_word(candidate_field, candidate_count) ||
            !memory.read_word(candidate_field + sizeof(uintptr_t), candidate_pointers) ||
            candidate_count < minimum_count || candidate_count >= best_count ||
            minimum_count * 2 < candidate_count ||
            !validate_pointer_array(memory, candidate_pointers, candidate_count, true)) continue;
        best_count = candidate_count;
        field_address = candidate_field;
        count = candidate_count;
        pointers = candidate_pointers;
    }
    return best_count != std::numeric_limits<uintptr_t>::max();
}

bool read_generic_arguments(const MemoryView &memory, const MetadataCandidate &metadata,
                            int32_t generic_inst_index, std::vector<uint64_t> &arguments) {
    if (generic_inst_index < 0) return true;
    if (static_cast<uintptr_t>(generic_inst_index) >= metadata.generic_insts_count) return false;
    uintptr_t generic_inst = 0;
    if (!memory.read_word(metadata.generic_insts + static_cast<uintptr_t>(generic_inst_index) *
                          sizeof(uintptr_t), generic_inst)) return false;
    uintptr_t argument_count = 0;
    uintptr_t argument_array = 0;
    if (!memory.read_word(generic_inst, argument_count) ||
        !memory.read_word(generic_inst + sizeof(uintptr_t), argument_array) ||
        argument_count > 64) return false;
    size_t bytes = 0;
    if (!multiply_fits(argument_count, sizeof(uintptr_t), bytes) ||
        (argument_count && !memory.contains(argument_array, bytes))) return false;
    for (uintptr_t index = 0; index < argument_count; ++index) {
        uintptr_t type_pointer = 0;
        if (!memory.read_word(argument_array + index * sizeof(uintptr_t), type_pointer) ||
            !memory.contains(type_pointer, sizeof(Il2CppType))) return false;
        arguments.push_back(type_pointer);
    }
    return true;
}

void decode_generic_instances(const MemoryView &memory, const MetadataCandidate &metadata,
                              size_t table_stride, uintptr_t generic_method_pointers,
                              uintptr_t generic_method_pointer_count,
                              std::vector<GenericMethodInstanceEntry> &output) {
    for (uintptr_t table_index = 0; table_index < metadata.generic_method_table_count; ++table_index) {
        const uintptr_t table_entry = metadata.generic_method_table + table_index * table_stride;
        int32_t method_spec_index = -1;
        int32_t method_pointer_index = -1;
        if (!memory.read_i32(table_entry, method_spec_index) ||
            !memory.read_i32(table_entry + 4, method_pointer_index) ||
            method_spec_index < 0 ||
            static_cast<uintptr_t>(method_spec_index) >= metadata.method_specs_count) continue;

        const uintptr_t method_spec = metadata.method_specs +
                                      static_cast<uintptr_t>(method_spec_index) * 12;
        int32_t method_definition_index = -1;
        int32_t class_inst_index = -1;
        int32_t method_inst_index = -1;
        if (!memory.read_i32(method_spec, method_definition_index) ||
            !memory.read_i32(method_spec + 4, class_inst_index) ||
            !memory.read_i32(method_spec + 8, method_inst_index) ||
            method_definition_index < 0) continue;

        GenericMethodInstanceEntry instance;
        instance.method_spec_index = method_spec_index;
        instance.method_definition_index = method_definition_index;
        instance.method_pointer_index = method_pointer_index;
        if (method_pointer_index >= 0 &&
            static_cast<uintptr_t>(method_pointer_index) < generic_method_pointer_count) {
            uintptr_t native_address = 0;
            if (memory.read_word(generic_method_pointers +
                                 static_cast<uintptr_t>(method_pointer_index) * sizeof(uintptr_t),
                                 native_address) && memory.contains(native_address, 1, true)) {
                instance.native_address = native_address;
            }
        }
        if (!read_generic_arguments(memory, metadata, class_inst_index,
                                    instance.class_type_arguments) ||
            !read_generic_arguments(memory, metadata, method_inst_index,
                                    instance.method_type_arguments)) continue;
        output.push_back(std::move(instance));
    }
}

void append_pointer_array(const MemoryView &memory, uintptr_t pointers, uintptr_t count,
                          const char *source, std::vector<RegistrationMethodPointer> &output) {
    for (uintptr_t index = 0; index < count; ++index) {
        uintptr_t pointer = 0;
        if (!memory.read_word(pointers + index * sizeof(uintptr_t), pointer)) break;
        if (pointer && memory.contains(pointer, 1, true)) output.push_back({pointer, source});
    }
}

void scan_metadata_slots(const MemoryView &memory,
                         const std::vector<MetadataTargetEntry> &targets,
                         std::vector<MetadataSlotEntry> &output) {
    std::unordered_map<uintptr_t, std::vector<const MetadataTargetEntry *>> targets_by_address;
    std::set<std::pair<uintptr_t, std::string>> unique_targets;
    for (const auto &target : targets) {
        if (target.address && unique_targets.emplace(target.address, target.kind).second) {
            targets_by_address[target.address].push_back(&target);
        }
    }
    if (targets_by_address.empty()) return;

    for (const auto &range : memory.ranges()) {
        if (range.executable || range.end - range.begin < sizeof(uintptr_t)) continue;
        uintptr_t cursor = (range.begin + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);
        for (; cursor + sizeof(uintptr_t) <= range.end; cursor += sizeof(uintptr_t)) {
            uintptr_t pointer = 0;
            if (!memory.read_word(cursor, pointer)) continue;
            const auto matches = targets_by_address.find(pointer);
            if (matches == targets_by_address.end()) continue;
            for (const auto *target : matches->second) {
                output.push_back({cursor, pointer, target->kind, target->id, target->name});
            }
        }
    }
}

} // namespace

RuntimeRegistrationInfo RuntimeRegistrationScanner::scan(
        size_t image_count, size_t type_definition_count,
        const std::vector<MetadataTargetEntry> &metadata_targets) const {
    RuntimeRegistrationInfo result;
    MemoryView memory;
    if (!memory.initialize()) {
        result.status = "libil2cpp memory map unavailable";
        return result;
    }

    const CodeGenCandidate codegen = find_codegen_modules(memory, image_count, result);
    if (codegen.field_address) {
        result.codegen_modules_found = true;
        result.codegen_modules_field_address = codegen.field_address;
        result.modules = codegen.modules;
        for (const auto &module : codegen.modules) {
            append_pointer_array(memory, module.method_pointers_address, module.method_pointer_count,
                                 "codegen_module_method_pointer", result.method_pointers);
        }
    }

    const MetadataCandidate metadata = find_metadata_registration(memory, type_definition_count);
    std::vector<MetadataTargetEntry> enriched_metadata_targets = metadata_targets;
    if (metadata.address) {
        result.metadata_registration_found = true;
        result.metadata_registration_address = metadata.address;
        result.generic_method_table_count = metadata.generic_method_table_count;
        result.method_specs_count = metadata.method_specs_count;
        for (uintptr_t index = 0; index < metadata.types_count; ++index) {
            uintptr_t type_pointer = 0;
            if (!memory.read_word(metadata.types + index * sizeof(uintptr_t), type_pointer) ||
                !memory.contains(type_pointer, sizeof(Il2CppType))) continue;
            const std::string type_id = "metadata-type:" + std::to_string(index);
            enriched_metadata_targets.push_back({type_pointer, "type_ref", type_id, ""});
            if (il2cpp_class_from_type) {
                auto klass = il2cpp_class_from_type(reinterpret_cast<const Il2CppType *>(type_pointer));
                if (klass) {
                    enriched_metadata_targets.push_back({reinterpret_cast<uint64_t>(klass), "type_info",
                                                         type_id, ""});
                }
            }
        }
    }

    if (codegen.field_address && metadata.address) {
        int32_t max_method_index = -1;
        const size_t table_stride = detect_generic_table_stride(memory, metadata, max_method_index);
        result.generic_table_stride = table_stride;
        if (table_stride) {
            uintptr_t field_address = 0;
            uintptr_t pointer_count = 0;
            uintptr_t pointers = 0;
            if (find_generic_method_pointers(memory, codegen, max_method_index, field_address,
                                             pointer_count, pointers)) {
                result.generic_method_pointers_found = true;
                result.generic_method_pointers_field_address = field_address;
                result.generic_method_pointer_count = pointer_count;
                append_pointer_array(memory, pointers, pointer_count,
                                     "registration_generic_method_pointer", result.method_pointers);
                decode_generic_instances(memory, metadata, table_stride, pointers, pointer_count,
                                         result.generic_instances);
            }
        }
    }

    if (result.codegen_modules_found && result.metadata_registration_found &&
        result.generic_method_pointers_found) {
        result.status = "registration located and validated";
    } else if (result.codegen_modules_found || result.metadata_registration_found) {
        result.status = "registration partially located";
    } else {
        result.status = "registration not found with high confidence";
    }
    scan_metadata_slots(memory, enriched_metadata_targets, result.metadata_slots);
    LOGI("Registration scan: %s", result.status.c_str());
    return result;
}
