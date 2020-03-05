/*
 * Proxy-WASM ABI.
 */
// NOLINT(namespace-envoy)

#pragma once

#include <stddef.h>
#include <stdint.h>

//
// ABI functions imported from the host into the VM for calls from the VM to the host.
//

// Configuration and Status

/**
 * Called from the VM to get bytes from a buffer (e.g. a vm or plugin configuration).
 * @param start is the offset of the first byte to retrieve.
 * @param length is the number of the bytes to retrieve. If start + length exceeds the number of
 * bytes available then configuration_size will be set to the number of bytes returned.
 * @param ptr a pointer to a location which will be filled with either nullptr (if no
 * data is available) or a pointer to a allocated block containing the configuration
 * bytes.
 * @param size a pointer to a location containing the size (or zero) of any returned
 * byte block.
 * @return a WasmResult: OK, InvalidArgument, InvalidMemoryAccess. Note: if OK is returned *ptr may
 * be nullptr.
 */
enum class WasmBufferType : int32_t {
  VmConfiguration = 0,
  PluginConfiguration = 1,
  MAX = 2,
};
extern "C" WasmResult proxy_get_buffer_bytes(WasmBufferType type, uint32_t start, uint32_t length,
                                             const char** ptr, size_t* size);

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
enum class WasmLogLevel : uint32_t {
  Trace = 0, Debug = 1, Info = 2, Warning = 3, Error = 4, Critical = 5,
}
extern "C" WasmResult proxy_log(WasmLogLevel level, const char* log_message, size_t log_message_size);

// System

/**
 * Called from the VM by a root context after returning zero from proxy_on_done() to indicate that
 * the root context is now done and the proxy_on_delete can be called and the VM shutdown and
 * deleted.
 * @return a WasmResult: OK, NotFound (if the caller did not previous return zero from
 * proxy_on_done()).
 */
extern "C" WasmResult proxy_done();
