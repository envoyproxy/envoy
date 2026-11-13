package abi

import (
	"unsafe"

	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

// HttpFilterEnvoyPointer returns the raw Envoy pointer of the stream behind h,
// the envoy_dynamic_module_type_http_filter_envoy_ptr that Envoy passed to the
// module. It returns false when h is not a handle created by this package, for
// example a fake in a test.
//
// This function is for experts only. Most modules never need it, and should use
// the methods of shared.HttpFilterHandle instead.
//
// The pointer lets a Go module hand a stream to native code in the same
// process, which then calls the envoy_dynamic_module_callback_http_* functions
// itself. Nothing checks how the pointer is used, and a wrong use crashes Envoy
// or corrupts its memory. The rules of the ABI apply:
//
//   - Use the pointer only on the worker thread that owns the stream, during a
//     callback of that stream or a function scheduled on it.
//   - Never use the pointer after the stream is destroyed.
//   - Buffers that Envoy returns are borrowed until the callback returns. Copy
//     them before keeping them.
func HttpFilterEnvoyPointer(h shared.HttpFilterHandle) (unsafe.Pointer, bool) {
	d, ok := h.(*dymHttpFilterHandle)
	if !ok || d == nil {
		return nil, false
	}
	return unsafe.Pointer(d.hostPluginPtr), true
}

// HttpFilterConfigEnvoyPointer returns the raw Envoy pointer of the filter
// config behind h, the envoy_dynamic_module_type_http_filter_config_envoy_ptr
// that Envoy passed to the module. It returns false when h is not a handle
// created by this package, for example a fake in a test.
//
// This function is for experts only. Most modules never need it, and should use
// the methods of shared.HttpFilterConfigHandle instead.
//
// The pointer lets a Go module hand the filter config to native code in the
// same process, which then calls the
// envoy_dynamic_module_callback_http_filter_config_* functions itself. Nothing
// checks how the pointer is used, and a wrong use crashes Envoy or corrupts its
// memory. The rules of the ABI apply:
//
//   - Define metrics only on the main thread, while the filter config is
//     created.
//   - Use the pointer only while the filter config lives. A stream created from
//     the filter config keeps it alive, so the pointer is valid during the
//     callbacks of that stream.
//   - Envoy destroys a filter config when it is no longer used, for example
//     after a listener update, and the destroy can happen during a function
//     scheduled on that config. Native code gets no notice of the destroy. A
//     pointer kept outside the life of its filter config is invalid.
//
// Two examples of a use after the destroy: defining a metric on a destroyed
// filter config crashes Envoy, and recording a value of a metric on one reads
// freed memory.
func HttpFilterConfigEnvoyPointer(h shared.HttpFilterConfigHandle) (unsafe.Pointer, bool) {
	d, ok := h.(*dymConfigHandle)
	if !ok || d == nil {
		return nil, false
	}
	return unsafe.Pointer(d.hostConfigPtr), true
}
