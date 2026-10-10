#include <stdlib.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

void* allocate_memory(size_t size) {
  char* memory = malloc(size);
  // Using the memory keeps the call to malloc out of tail position, so this frame is on its stack.
  if (memory != NULL) {
    memory[0] = 1;
  }
  return memory;
}

void free_memory(void* memory) { free(memory); }
