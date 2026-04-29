package abi

/*
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup
#cgo linux LDFLAGS: -Wl,--unresolved-symbols=ignore-all
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../../abi/abi.h"
*/
import "C"
import (
	"runtime"
	"unsafe"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

type certValidatorWrapper struct {
	validator shared.CertValidator
	// digestCache holds the most-recent digest bytes returned from UpdateDigest, keeping the
	// memory alive for the duration of the on_update_digest call.
	digestCache []byte
}

var certValidatorManager = newManager[certValidatorWrapper]()

// dymCertValidatorContext implements shared.CertValidatorContext. It is bound to a single
// VerifyCertChain call.
type dymCertValidatorContext struct {
	hostConfigPtr C.envoy_dynamic_module_type_cert_validator_config_envoy_ptr
}

func (c *dymCertValidatorContext) SetErrorDetails(errorDetails string) {
	C.envoy_dynamic_module_callback_cert_validator_set_error_details(
		c.hostConfigPtr, stringToModuleBuffer(errorDetails))
	runtime.KeepAlive(errorDetails)
}

func (c *dymCertValidatorContext) SetFilterState(key, value string) bool {
	ret := C.envoy_dynamic_module_callback_cert_validator_set_filter_state(
		c.hostConfigPtr, stringToModuleBuffer(key), stringToModuleBuffer(value))
	runtime.KeepAlive(key)
	runtime.KeepAlive(value)
	return bool(ret)
}

func (c *dymCertValidatorContext) GetFilterState(key string) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_cert_validator_get_filter_state(
		c.hostConfigPtr, stringToModuleBuffer(key), &buf)
	runtime.KeepAlive(key)
	if !bool(ret) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ret)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

// =============================================================================
// Event hooks
// =============================================================================

//export envoy_dynamic_module_on_cert_validator_config_new
func envoy_dynamic_module_on_cert_validator_config_new(
	_ C.envoy_dynamic_module_type_cert_validator_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_cert_validator_config_module_ptr {
	nameStr := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesUnsafe(config)

	configFactory := sdk.GetCertValidatorConfigFactory(nameStr)
	if configFactory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load cert validator configuration: no factory for %s", []any{nameStr})
		return nil
	}
	v, err := configFactory.Create(nameStr, configBytes)
	if err != nil || v == nil {
		hostLog(shared.LogLevelWarn, "Failed to load cert validator configuration: %v", []any{err})
		return nil
	}
	wrapper := &certValidatorWrapper{validator: v}
	configPtr := certValidatorManager.record(wrapper)
	return C.envoy_dynamic_module_type_cert_validator_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_cert_validator_config_destroy
func envoy_dynamic_module_on_cert_validator_config_destroy(
	configPtr C.envoy_dynamic_module_type_cert_validator_config_module_ptr,
) {
	w := certValidatorManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil {
		return
	}
	if w.validator != nil {
		w.validator.OnDestroy()
	}
	certValidatorManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_cert_validator_do_verify_cert_chain
func envoy_dynamic_module_on_cert_validator_do_verify_cert_chain(
	hostConfigPtr C.envoy_dynamic_module_type_cert_validator_config_envoy_ptr,
	configPtr C.envoy_dynamic_module_type_cert_validator_config_module_ptr,
	certs *C.envoy_dynamic_module_type_envoy_buffer,
	certsCount C.size_t,
	hostName C.envoy_dynamic_module_type_envoy_buffer,
	isServer C.bool,
) C.envoy_dynamic_module_type_cert_validator_validation_result {
	w := certValidatorManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil || w.validator == nil {
		return C.envoy_dynamic_module_type_cert_validator_validation_result{
			status:          C.envoy_dynamic_module_type_cert_validator_validation_status_Failed,
			detailed_status: C.envoy_dynamic_module_type_cert_validator_client_validation_status_Failed,
		}
	}
	certBufs := envoyBufferSliceToUnsafeEnvoyBufferSlice(unsafe.Slice(certs, int(certsCount)))
	hostNameStr := envoyBufferToStringUnsafe(hostName)
	ctx := &dymCertValidatorContext{hostConfigPtr: hostConfigPtr}
	result := w.validator.VerifyCertChain(ctx, certBufs, hostNameStr, bool(isServer))

	out := C.envoy_dynamic_module_type_cert_validator_validation_result{
		status:          C.envoy_dynamic_module_type_cert_validator_validation_status(result.Status),
		detailed_status: C.envoy_dynamic_module_type_cert_validator_client_validation_status(result.DetailedStatus),
		has_tls_alert:   C.bool(result.HasTLSAlert),
	}
	if result.HasTLSAlert {
		out.tls_alert = C.uint8_t(result.TLSAlert)
	}
	return out
}

//export envoy_dynamic_module_on_cert_validator_get_ssl_verify_mode
func envoy_dynamic_module_on_cert_validator_get_ssl_verify_mode(
	configPtr C.envoy_dynamic_module_type_cert_validator_config_module_ptr,
	handshakerProvidesCertificates C.bool,
) C.int {
	w := certValidatorManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil || w.validator == nil {
		return 0
	}
	return C.int(w.validator.GetSSLVerifyMode(bool(handshakerProvidesCertificates)))
}

//export envoy_dynamic_module_on_cert_validator_update_digest
func envoy_dynamic_module_on_cert_validator_update_digest(
	configPtr C.envoy_dynamic_module_type_cert_validator_config_module_ptr,
	outData *C.envoy_dynamic_module_type_module_buffer,
) {
	w := certValidatorManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil || w.validator == nil {
		return
	}
	digest := w.validator.UpdateDigest()
	w.digestCache = digest // keep alive for the duration of this call
	if len(digest) == 0 {
		outData.ptr = nil
		outData.length = 0
		return
	}
	outData.ptr = (*C.char)(unsafe.Pointer(unsafe.SliceData(digest)))
	outData.length = C.size_t(len(digest))
}
