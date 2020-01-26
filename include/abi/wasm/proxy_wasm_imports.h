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
 * Called from the VM to get any configuration. Valid only when in proxy_on_start() (where it will
 * return a VM configuration), proxy_on_configure() (where it will return a plugin configuration) or
 * in proxy_validate_configuration() (where it will return a VM configuration before
 * proxy_on_start() has been called and a plugin configuration after).
 * @param start is the offset of the first byte to retrieve.
 * @param length is the number of the bytes to retrieve. If start + length exceeds the number of
 * bytes available then configuration_size will be set to the number of bytes returned.
 * @param configuration_ptr a pointer to a location which will be filled with either nullptr (if no
 * configuration is available) or a pointer to a allocated block containing the configuration
 * bytes.
 * @param configuration_size a pointer to a location containing the size (or zero) of any returned
 * configuration byte block.
 * @return a WasmResult: OK, InvalidMemoryAccess. Note: if OK is returned  *configuration_ptr may
 * be nullptr.
 */
extern "C" WasmResult proxy_get_configuration((uint32_t start, uint32_t length,
      const char** configuration_ptr, size_t* configuration_size);

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
