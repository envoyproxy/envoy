/*
 * Proxy-WASM ABI.
 */
// NOLINT(namespace-envoy)

#pragma once

#include <stddef.h>
#include <stdint.h>

//
//  ABI calls from the host into the VM.
//
//  These will typically be implemented by a language specific SDK which will provide an API on top
//  of this ABI e.g. the C++ SDK provides a proxy_wasm_api.h implemenation of the API on top of this
//  ABI.
//

// Non-stream calls.

/**
 * Called when the VM starts for each plugin.
 * @param root_context_id is an identifier with the lifetime of the VM which will be used for
 * one or more related plugins and their corresponding proxy_on_configure(), proxy_on_done() and
 * proxy_on_delete() calls. It is also be provided during creation of stream context for those
 * plugins.
 * @param configuration_size is the size of any configuration available via proxy_get_configuration.
 * @return non-zero on success and zero on failure (e.g. bad configuration).
 */
extern "C" uint32_t proxy_on_start(uint32_t root_context_id, uint32_t configuration_size);
/**
 * Can be called to validate a configuration (e.g. by an xDS provider) both before proxy_on_start()
 * to verify the VM configuration or after proxy_on_start() to verify a plugin configuation.
 * @param root_context_id is a unique identifier for the configuration verification context.
 * @param configuration_size is the size of any configuration available via
 * proxy_get_configuration().
 * @return non-zero on success and zero on failure (i.e. bad configuration).
 */
extern "C" uint32_t proxy_validate_configuration(uint32_t root_context_id,
                                                 uint32_t configuration_size);
/**
 * Called when a plugin loads or when plugin configuration changes dynamically.
 * @param root_context_id is an identifier for one or more related plugins.
 * @param configuration_size is the size of any configuration available via
 * proxy_get_configuration().
 * @return non-zero on success and zero on failure (e.g. bad configuration).
 */
extern "C" uint32_t proxy_on_configure(uint32_t root_context_id, uint32_t configuration_size);

// Stream calls.

/**
 * Called when a request, stream or other ephemeral context is created.
 * @param context_id is an identifier the ephemeral context.
 * @param configuration_size is the size of any configuration available via
 * proxy_get_configuration().
 */
extern "C" void proxy_on_create(uint32_t context_id, uint32_t root_context_id);

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
extern "C" uint32_t proxy_on_done(uint32_t context_id);
/**
 * Called when the context is being deleted and will no longer receive any more calls.
 * @param context_id is an identifier the context.
 */
extern "C" void proxy_on_delete(uint32_t context_id);

//
// ABI calls from the VM to the host.
//

// Configuration and Status

/**
 * Called from the VM to get any configuration. Valid only when in a proxy_on_start(),
 * proxy_validate_configuration() or proxy_on_configure() handler.
 * @param configuration_ptr a pointer to a location which will be filled with either nullptr (if no
 * configuration is available) or a pointer to a malloc()ed block containing the configuration
 * bytes.
 * @param configuration_size a pointer to a location containing the size (or zero) of any returned
 * configuration byte block.
 * @return a WasmResult: OK, InvalidMemoryAccess. Note: if OK is returned  *configuration_ptr may
 * be nullptr.
 */
extern "C" WasmResult proxy_get_configuration(const char** configuration_ptr,
                                              size_t* configuration_size);

// Logging
//
// level: trace = 0, debug = 1, info = 2, warn = 3, error = 4, critical = 5

/**
 * Called from the VM to log a message.
 * @param level is one of trace = 0, debug = 1, info = 2, warn = 3, error = 4, critical = 5.
 * @param log_message is a pointer to a message to log.
 * @param log_message_size is the size of the message. Messages need not have a newline or be null
 * terminated.
 * @return a WasmResult: OK, InvalidMemoryAccess.
 */
extern "C" WasmResult proxy_log(uint32_t level, const char* log_message, size_t log_message_size);

// System

/**
 * Called from the VM by a root context after returning zero from proxy_on_done() to indicate that
 * the root context is now done and the proxy_on_delete can be called and the VM shutdown nand
 * deleted.
 * @return a WasmResult: OK, NotFound (if the caller did not previous return zero from
 * proxy_on_done()).
 */
extern "C" WasmResult proxy_done();
