// C trampolines for cgo callbacks that are passed as function pointers across
// the Envoy dynamic-module ABI. These cannot live in the cgo preamble of
// bootstrap.go: each cgo-translation unit emits its own copy of any `static`
// functions in the preamble, and Go's address-of (`C.cgoBootstrapCounterIteratorC`)
// generates a relocation against an *external* symbol of that name. With
// `static` the symbol has internal linkage and the dynamic linker fails with
// `undefined symbol: cgoBootstrapCounterIteratorC` at dlopen time. Defining
// the trampolines once in a stand-alone .c file gives them external linkage
// and a single definition, satisfying the relocation without duplicate-symbol
// link errors.
#include <stdint.h>
#include "../../../abi/abi.h"

// Forward declarations for the Go-exported callbacks. The Go runtime emits
// these symbols (lower-cased exactly as written) as part of building the
// shared library that contains the cgo package.
extern envoy_dynamic_module_type_stats_iteration_action cgoBootstrapCounterIteratorGo(
    envoy_dynamic_module_type_envoy_buffer name, uint64_t value, void* user_data);
extern envoy_dynamic_module_type_stats_iteration_action cgoBootstrapGaugeIteratorGo(
    envoy_dynamic_module_type_envoy_buffer name, uint64_t value, void* user_data);

envoy_dynamic_module_type_stats_iteration_action cgoBootstrapCounterIteratorC(
    envoy_dynamic_module_type_envoy_buffer name, uint64_t value, void* user_data) {
  return cgoBootstrapCounterIteratorGo(name, value, user_data);
}

envoy_dynamic_module_type_stats_iteration_action cgoBootstrapGaugeIteratorC(
    envoy_dynamic_module_type_envoy_buffer name, uint64_t value, void* user_data) {
  return cgoBootstrapGaugeIteratorGo(name, value, user_data);
}
