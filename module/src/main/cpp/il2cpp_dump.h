//
// Created by Perfare on 2020/7/4.
//

#ifndef ZYGISK_IL2CPPDUMPER_IL2CPP_DUMP_H
#define ZYGISK_IL2CPPDUMPER_IL2CPP_DUMP_H

#include <cstdint>

// Base address of libil2cpp.so in memory (set by il2cpp_api_init)
extern uint64_t il2cpp_base;

bool il2cpp_api_init(void *handle);

void il2cpp_dump(const char *outDir);

#endif //ZYGISK_IL2CPPDUMPER_IL2CPP_DUMP_H
