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

// Non-stream calls.
extern "C" uint32_t proxy_on_start(uint32_t root_context_id, uint32_t configuration_size);
extern "C" uint32_t proxy_validate_configuration(uint32_t root_context_id,
                                                 uint32_t configuration_size);
extern "C" uint32_t proxy_on_configure(uint32_t root_context_id, uint32_t configuration_size);

// Stream calls.
extern "C" void proxy_on_create(uint32_t context_id, uint32_t root_context_id);

// Stream and Non-stream calls.
extern "C" uint32_t proxy_on_done(uint32_t context_id);
extern "C" void proxy_on_delete(uint32_t context_id);

//
// ABI calls from the VM to the host.
//

// Configuration and Status
extern "C" WasmResult proxy_get_configuration(const char** configuration_ptr,
                                              size_t* configuration_size);

// Logging
// level: trace = 0, debug = 1, info = 2, warn = 3, error = 4, critical = 5
extern "C" WasmResult proxy_log(uint32_t level, const char* log_message, size_t log_message_size);

// System
extern "C" WasmResult proxy_done();
