#include <sched.h>
#include <stdatomic.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

static atomic_bool init_entered;
static atomic_bool init_released;
static atomic_bool init_finished;

// Blocks until the test releases it, so that the test can load the module again meanwhile.
envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  atomic_store(&init_entered, true);
  while (!atomic_load(&init_released)) {
    sched_yield();
  }
  atomic_store(&init_finished, true);
  return envoy_dynamic_modules_abi_version;
}

bool get_init_entered(void) { return atomic_load(&init_entered); }

void release_init(void) { atomic_store(&init_released, true); }

bool get_init_finished(void) { return atomic_load(&init_finished); }
