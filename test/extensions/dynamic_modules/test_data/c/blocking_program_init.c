#include <stdatomic.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

static atomic_bool init_released;
static atomic_bool init_finished;
static atomic_int running_inits;
static atomic_int most_running_inits;
static atomic_int finished_inits;

// Blocks until the test releases it, so that the test can load the module again meanwhile.
envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  const int running = atomic_fetch_add(&running_inits, 1) + 1;
  int most = atomic_load(&most_running_inits);
  while (running > most && !atomic_compare_exchange_weak(&most_running_inits, &most, running)) {
  }
  while (!atomic_load(&init_released)) {
  }
  atomic_fetch_sub(&running_inits, 1);
  atomic_fetch_add(&finished_inits, 1);
  atomic_store(&init_finished, true);
  return envoy_dynamic_modules_abi_version;
}

bool get_init_entered(void) { return atomic_load(&most_running_inits) > 0; }

bool get_init_finished(void) { return atomic_load(&init_finished); }

int get_most_running_inits(void) { return atomic_load(&most_running_inits); }

int get_finished_inits(void) { return atomic_load(&finished_inits); }

void release_init(void) { atomic_store(&init_released, true); }

// Restores the initial state, which a statically linked copy keeps between tests.
void reset_init(void) {
  atomic_store(&init_released, false);
  atomic_store(&init_finished, false);
  atomic_store(&running_inits, 0);
  atomic_store(&most_running_inits, 0);
  atomic_store(&finished_inits, 0);
}
