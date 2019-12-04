/*
 * Intrinsic functions for WASM modules.
 */
// NOLINT(namespace-envoy)

#pragma once

#include "stddef.h"

//
//  ABI calls into the VM.
//

// Non-stream calls.
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t proxy_on_start(uint32_t root_context_id,
                                                        uint32_t configuration_size);
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t proxy_validate_configuration(uint32_t root_context_id,
                                                                      uint32_t configuration_size);
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t proxy_on_configure(uint32_t root_context_id,
                                                            uint32_t configuration_size);

// Stream calls.
extern "C" EMSCRIPTEN_KEEPALIVE void proxy_on_create(uint32_t context_id, uint32_t root_context_id);

// Stream and Non-stream calls.
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t proxy_on_done(uint32_t context_id);
extern "C" EMSCRIPTEN_KEEPALIVE void proxy_on_delete(uint32_t context_id);

//
// ABI calls from the VM.
//

// Configuration and Status
extern "C" WasmResult proxy_get_configuration(const char** configuration_ptr,
                                              size_t* configuration_size);

// Logging
extern "C" WasmResult proxy_log(LogLevel level, const char* logMessage, size_t messageSize);

// Time
extern "C" WasmResult proxy_get_current_time_nanoseconds(uint64_t* nanoseconds);

// Metrics
extern "C" WasmResult proxy_define_metric(MetricType type, const char* name_ptr, size_t name_size,
                                          uint32_t* metric_id);
extern "C" WasmResult proxy_increment_metric(uint32_t metric_id, int64_t offset);
extern "C" WasmResult proxy_record_metric(uint32_t metric_id, uint64_t value);
extern "C" WasmResult proxy_get_metric(uint32_t metric_id, uint64_t* result);

// System
extern "C" WasmResult proxy_done();
