package abi

/*
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "../../../abi/abi.h"
*/
import "C"

import (
	"unsafe"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

////////////////////////////////////////////////////////////////////////////////////////////////////
// HTTP Header Formatter
////////////////////////////////////////////////////////////////////////////////////////////////////

// headerFormatterConfigWrapper holds the per-config Go state that must stay alive for as long as
// Envoy keeps the in-module config pointer. It is kept alive by headerFormatterConfigManager,
// which also maps the pointer back to the wrapper.
type headerFormatterConfigWrapper struct {
	config       shared.HeaderFormatterConfig
	configHandle *dymHeaderFormatterConfigHandle
}

// headerFormatterWrapper holds the per-message formatter. Its methods are called by the single
// worker thread that owns the message, so it needs no synchronization.
type headerFormatterWrapper struct {
	formatter shared.HeaderFormatter
	// handle wraps the Envoy-side pointer to this formatter and is handed to the module when the
	// formatter is created. It is kept here so it lives exactly as long as the formatter does,
	// whether or not the module retained it.
	handle *dymHeaderFormatterHandle
}

// headerFormatterConfigManager keeps each headerFormatterConfigWrapper alive for as long as Envoy
// holds the in-module config pointer and maps that pointer back to the wrapper.
var headerFormatterConfigManager = newManager[headerFormatterConfigWrapper]()

// headerFormatterManager does the same for the per-message formatters. unwrap is lock-free, which
// matters because the format hook runs for every header of every message.
var headerFormatterManager = newManager[headerFormatterWrapper]()

// dymHeaderFormatterConfigHandle implements shared.HeaderFormatterConfigHandle. Header formatting
// exposes no config-scoped callbacks, so it only carries the Envoy-side configuration pointer for
// future use.
type dymHeaderFormatterConfigHandle struct {
	hostConfigPtr C.envoy_dynamic_module_type_header_formatter_config_envoy_ptr
}

func (h *dymHeaderFormatterConfigHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}

func (h *dymHeaderFormatterConfigHandle) GetLogLevel() shared.LogLevel {
	return shared.LogLevel(C.envoy_dynamic_module_callback_get_log_level())
}

func (h *dymHeaderFormatterConfigHandle) IsLogLevelEnabled(level shared.LogLevel) bool {
	return bool(C.envoy_dynamic_module_callback_log_enabled(
		(C.envoy_dynamic_module_type_log_level)(uint32(level)),
	))
}

// dymHeaderFormatterHandle implements shared.HeaderFormatterHandle. One is created per formatter
// and passed to Create. Header formatting exposes no formatter-scoped callbacks, so it only carries
// the Envoy-side formatter pointer for future use.
type dymHeaderFormatterHandle struct {
	hostFormatterPtr C.envoy_dynamic_module_type_header_formatter_envoy_ptr
}

func (h *dymHeaderFormatterHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}

func (h *dymHeaderFormatterHandle) GetLogLevel() shared.LogLevel {
	return shared.LogLevel(C.envoy_dynamic_module_callback_get_log_level())
}

func (h *dymHeaderFormatterHandle) IsLogLevelEnabled(level shared.LogLevel) bool {
	return bool(C.envoy_dynamic_module_callback_log_enabled(
		(C.envoy_dynamic_module_type_log_level)(uint32(level)),
	))
}

//export envoy_dynamic_module_on_header_formatter_config_new
func envoy_dynamic_module_on_header_formatter_config_new(
	hostConfigPtr C.envoy_dynamic_module_type_header_formatter_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_header_formatter_config_module_ptr {
	nameString := envoyBufferToStringUnsafe(name)
	configBuffer := envoyBufferToUnsafeEnvoyBuffer(config)

	configHandle := &dymHeaderFormatterConfigHandle{hostConfigPtr: hostConfigPtr}
	formatterConfig, err := sdk.NewHeaderFormatterConfig(configHandle, nameString, configBuffer)
	if err != nil {
		configHandle.Log(shared.LogLevelWarn,
			"Failed to load header formatter configuration for %q: %v", nameString, err)
		return nil
	}
	if formatterConfig == nil {
		configHandle.Log(shared.LogLevelWarn,
			"Failed to load header formatter configuration for %q: config is nil", nameString)
		return nil
	}

	configPtr := headerFormatterConfigManager.record(&headerFormatterConfigWrapper{
		config:       formatterConfig,
		configHandle: configHandle,
	})
	return C.envoy_dynamic_module_type_header_formatter_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_header_formatter_config_destroy
func envoy_dynamic_module_on_header_formatter_config_destroy(
	configPtr C.envoy_dynamic_module_type_header_formatter_config_module_ptr,
) {
	wrapper := headerFormatterConfigManager.unwrap(unsafe.Pointer(configPtr))
	if wrapper == nil {
		return
	}
	wrapper.config.OnDestroy()
	headerFormatterConfigManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_header_formatter_new
func envoy_dynamic_module_on_header_formatter_new(
	configPtr C.envoy_dynamic_module_type_header_formatter_config_module_ptr,
	hostFormatterPtr C.envoy_dynamic_module_type_header_formatter_envoy_ptr,
) C.envoy_dynamic_module_type_header_formatter_module_ptr {
	wrapper := headerFormatterConfigManager.unwrap(unsafe.Pointer(configPtr))
	if wrapper == nil {
		// A null formatter makes Envoy fall back to the default header casing for this message.
		return nil
	}
	// The handle is created before the formatter so it can be handed to it, and outlives every
	// formatter hook, which is why the module may keep it.
	handle := &dymHeaderFormatterHandle{hostFormatterPtr: hostFormatterPtr}
	formatter := wrapper.config.Create(handle)
	if formatter == nil {
		return nil
	}
	formatterPtr := headerFormatterManager.record(&headerFormatterWrapper{
		formatter: formatter,
		handle:    handle,
	})
	return C.envoy_dynamic_module_type_header_formatter_module_ptr(formatterPtr)
}

//export envoy_dynamic_module_on_header_formatter_destroy
func envoy_dynamic_module_on_header_formatter_destroy(
	formatterPtr C.envoy_dynamic_module_type_header_formatter_module_ptr,
) {
	headerFormatterManager.remove(unsafe.Pointer(formatterPtr))
}

//export envoy_dynamic_module_on_header_formatter_process_key
func envoy_dynamic_module_on_header_formatter_process_key(
	// The handle the formatter was created with already wraps this pointer.
	_ C.envoy_dynamic_module_type_header_formatter_envoy_ptr,
	formatterPtr C.envoy_dynamic_module_type_header_formatter_module_ptr,
	key C.envoy_dynamic_module_type_envoy_buffer,
) {
	wrapper := headerFormatterManager.unwrap(unsafe.Pointer(formatterPtr))
	if wrapper == nil {
		return
	}
	// The key is handed over as an unsafe view of Envoy-owned memory: it is reused after this
	// call, so a module that remembers keys copies it with ToString or ToBytes.
	wrapper.formatter.ProcessKey(envoyBufferToUnsafeEnvoyBuffer(key))
}

//export envoy_dynamic_module_on_header_formatter_format
func envoy_dynamic_module_on_header_formatter_format(
	// The handle the formatter was created with already wraps this pointer.
	_ C.envoy_dynamic_module_type_header_formatter_envoy_ptr,
	formatterPtr C.envoy_dynamic_module_type_header_formatter_module_ptr,
	key C.envoy_dynamic_module_type_envoy_buffer,
	result *C.envoy_dynamic_module_type_module_buffer,
) C.bool {
	wrapper := headerFormatterManager.unwrap(unsafe.Pointer(formatterPtr))
	if wrapper == nil {
		return C.bool(false)
	}
	formatted, ok := wrapper.formatter.Format(envoyBufferToUnsafeEnvoyBuffer(key))
	if !ok {
		return C.bool(false)
	}
	// Envoy is handed the string's own bytes: keeping them alive until the next call into the
	// module - and so reachable from the Go heap while Envoy copies them - is the formatter's
	// side of the shared.HeaderFormatter contract.
	*result = stringToModuleBuffer(formatted)
	return C.bool(true)
}
