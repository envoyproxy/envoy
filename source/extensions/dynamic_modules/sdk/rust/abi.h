#pragma once

// NOLINT(namespace-envoy)

// This is a pure C header file that defines the ABI of the core of dynamic modules used by Envoy.
//
// This must not contain any dependencies besides standard library since it is not only used by
// Envoy itself but also by dynamic module SDKs written in non-C++ languages.
//
// Currently, compatibility is only guaranteed by an exact version match between the Envoy
// codebase and the dynamic module SDKs. In the future, after the ABI is stabilized, we will revisit
// this restriction and hopefully provide a wider compatibility guarantee. Until then, Envoy
// checks the hash of the ABI header files to ensure that the dynamic modules are built against the
// same version of the ABI.

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// ---------------------------------- Types ------------------------------------
// -----------------------------------------------------------------------------
//
// Types used in the ABI. The name of a type must be prefixed with "envoy_dynamic_module_type_".

/**
 * envoy_dynamic_module_type_abi_version represents a null-terminated string that contains the ABI
 * version of the dynamic module. This is used to ensure that the dynamic module is built against
 * the compatible version of the ABI.
 */
typedef const char* envoy_dynamic_module_type_abi_version; // NOLINT(modernize-use-using)

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
 * loaded. The function returns the ABI version of the dynamic module. If null is returned, the
 * module will be unloaded immediately.
 *
 * For Envoy, the return value will be used to check the compatibility of the dynamic module.
 *
 * For dynamic modules, this is useful when they need to perform some process-wide
 * initialization or check if the module is compatible with the platform, such as CPU features.
 * Note that initialization routines of a dynamic module can also be performed without this function
 * through constructor functions in an object file. However, normal constructors cannot be used
 * to check compatibility and gracefully fail the initialization because there is no way to
 * report an error to Envoy.
 *
 * @return envoy_dynamic_module_type_abi_version is the ABI version of the dynamic module. Null
 *         means the error and the module will be unloaded immediately.
 */
envoy_dynamic_module_type_abi_version envoy_dynamic_module_on_program_init();

#ifdef __cplusplus
}
#endif
