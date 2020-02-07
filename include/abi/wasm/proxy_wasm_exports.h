/*
 * Proxy-WASM ABI.
 */
// NOLINT(namespace-envoy)

#pragma once

#include <stddef.h>
#include <stdint.h>

//
// ABI functions export from the VM to the host for calls from the host into the VM.
//
// These will typically be implemented by a language specific SDK which will provide an API on top
// of this ABI e.g. the C++ SDK provides a proxy_wasm_api.h implementation of the API on top of
// this ABI.
//
// The Wasm VM can only access memory in the VM. Consequently, all data must be passed as integral
// call parameters or by the host allocating memory in the VM which is then owned by the Wasm code.
// For consistency and to enable diverse Wasm languages (e.g. languages with GC), the ABI uses a
// single mechanism for allocating memory in the VM and requires that all memory allocations be
// explicitly requested by calls from the VM and that the Wasm code then owns the allocated memory.
//

// Non-stream calls.

/**
 * Called when the VM starts by the first plugin to use the VM.
 * @param root_context_id is an identifier for one or more related plugins.
 * @param vm_configuration_size is the size of any configuration available via
 * proxy_get_configuration during the lifetime of this call.
 * @return non-zero on success and zero on failure (e.g. bad configuration).
 */
enum class WasmOnVmStartResult : uint32_t {
  Ok = 0,
  BadConfiguration = 1,
};
extern "C" WasmOnVmStartResult proxy_on_vm_start(uint32_t root_context_id,
                                                 uint32_t vm_configuration_size);

/**
 * Can be called to validate a configuration (e.g. from bootstrap or xDS) both before
 * proxy_on_start() to verify the VM configuration or after proxy_on_start() to verify a plugin
 * configuration.
 * @param root_context_id is a unique identifier for the configuration verification context.
 * @param configuration_size is the size of any configuration available via
 * proxy_get_configuration().
 * @return non-zero on success and zero on failure (i.e. bad configuration).
 */
enum class WasmOnValidateConfigurationResult : uint32_t {
  Ok = 0,
  BadConfiguration = 1,
};
extern "C" WasmOnValidateConfigurationResult
proxy_validate_configuration(uint32_t root_context_id, uint32_t configuration_size);
/**
 * Called when a plugin loads or when plugin configuration changes dynamically.
 * @param root_context_id is an identifier for one or more related plugins.
 * @param plugin_configuration_size is the size of any configuration available via
 * proxy_get_configuration().
 * @return non-zero on success and zero on failure (e.g. bad configuration).
 */
enum class WasmOnConfigureResult : uint32_t {
  Ok = 0,
  BadConfiguration = 1,
}
extern "C" WasmOnConfigureResult proxy_on_configure(uint32_t root_context_id,
                                                    uint32_t plugin_configuration_size);

// Stream calls.

/**
 * Called when a request, stream or other ephemeral context is created.
 * @param context_id is an identifier of the ephemeral context.
 * @param configuration_size is the size of any configuration available via
 * proxy_get_configuration().
 */
extern "C" void proxy_on_context_create(uint32_t context_id, uint32_t root_context_id);

// Stream and Non-stream calls.

/**
 * For stream contexts, called when the stream has completed. Note: if applicable proxy_on_log() is
 * called after proxy_on_done() and before proxy_on_delete(). For root contexts, proxy_on_done() is
 * called when the VM is going to shutdown.
 * @param context_id is an identifier the context.
 * @return non-zero to indicate that this context is done. Stream contexts must return non-zero.
 * Root contexts may return zero to defer the VM shutdown and the proxy_on_delete call until after a
 * future proxy_done() call by the root context.
 */
enum class WasmOnDoneResult : uint32_t {
  Done = 0,
  NotDone = 1,
}
extern "C" WasmOnDoneResult proxy_on_done(uint32_t context_id);

/**
 * Called when the context is being deleted and will no longer receive any more calls.
 * @param context_id is an identifier the context.
 */
extern "C" void proxy_on_delete(uint32_t context_id);
