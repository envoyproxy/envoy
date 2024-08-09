#pragma once

// NOLINT(namespace-envoy)

// This is a pure single C header file that defines the ABI of the dynamic module
// used by Envoy.
//
// This must not contain any dependencies besides standard library since it is not only used by
// Envoy itself but also by dynamic module SDKs written in non-C++ languages.
//
// Currently, compatibility is only guaranteed by an exact version match between the Envoy
// codebase and the dynamic module SDKs. In the future, after the ABI is stabilized, we will revisit
// this restriction and hopefully provide a wider compatibility guarantee. Until then, we use the
// simple symbol prefix "envoy_dynamic_module_" without any versioning at the symbol level.
//
// All ABI definitions, regardless of the area of usage, must be included in this single header
// file. This is not only to ensure easy inclusion by any language bindings but also because the ABI
// must be designed in a way that a single object file can be used at multiple extension points
// (e.g., multiple TCP/HTTP filter chains) within the same Envoy process. The latter requirement
// stems from the fact that some languages require exactly one shared library per process, such as
// Go (https://github.com/golang/go/issues/65050). Additionally, it is important to reduce the
// memory footprint of the entire Envoy process to avoid loading multiple copies of the same
// language runtime in the same process.
//
// In other words, this file serves as the central reference point for any programming language to
// build dynamic modules to interact with Envoy.
//
// Each SDK can be used outside of the Envoy source tree with the normal build tools for the
// language without needing to clone the Envoy source tree. To accomplish this, each SDK directory
// contains an identical hard copy of the `abi.h` file (e.g., Go and Rust build systems don't work
// well with symlinks).

#ifdef __cplusplus
#include <cstddef>

extern "C" {
#else
#include <stddef.h>
#endif

// -----------------------------------------------------------------------------
// ---------------------------------- Types ------------------------------------
// -----------------------------------------------------------------------------
//
// Types used in the ABI. The name of a type must be prefixed with "envoy_dynamic_module_type_".

/**
 * envoy_dynamic_module_type_program_init_result is the return type of
 * envoy_dynamic_module_on_program_init. 0 on success, non-zero on failure.
 */
typedef size_t envoy_dynamic_module_type_program_init_result; // NOLINT(modernize-use-using)

// -----------------------------------------------------------------------------
// ------------------------------- Event Hooks ---------------------------------
// -----------------------------------------------------------------------------
//
// Event hooks are functions that are called by Envoy in response to certain events.
// The module must implement and export these functions in the dynamic module object file.
//
// Each event hook is defined as a function prototype. The symbol must be prefixed with
// "envoy_dynamic_module_on_".

/**
 * envoy_dynamic_module_on_program_init is called by the main thread exactly when the module is
 * loaded. The function returns 0 on success and non-zero on failure. This is useful when a module
 * needs to perform some process-wide initialization or check if the module is compatible with the
 * platform, such as CPU features. If the function returns non-zero, the module will be unloaded
 * immediately. The status code will be logged by Envoy.
 *
 * Note that initialization routines of a dynamic module can also be performed without this function
 * through constructor functions in an object file. However, normal constructors cannot be used
 * to check compatibility and gracefully fail the initialization because there is no way to
 * return the status to Envoy.
 *
 * @return envoy_dynamic_module_type_program_init_result 0 on success, non-zero on failure.
 */
envoy_dynamic_module_type_program_init_result envoy_dynamic_module_on_program_init();

#ifdef __cplusplus
}
#endif
