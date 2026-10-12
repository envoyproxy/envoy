// Package recovery provides the panic barrier for the cgo exports of the dynamic module Go SDK. A
// Go panic that crosses a cgo export aborts the whole process, so every export defers Export or
// ExportVoid to turn a module panic into a logged, fail-closed return. The logic lives in this
// pure Go package so it is unit-testable without the Envoy host, while the abi package supplies the
// host-backed logger through Logger.
package recovery

// Logger reports a recovered panic. The abi package sets it once, before any worker thread runs, to
// an Envoy host-backed error logger. The default is a no-op so the primitives stay usable in tests.
var Logger = func(functionName string, recovered any) {}

// Export sets failClosed as the return value when a hook panics. It is deferred at the top of
// every value-returning export as defer recovery.Export(functionName, failClosed, &ret). It mirrors
// the Rust and C++ SDK panic barriers.
func Export[T any](functionName string, failClosed T, ret *T) {
	if r := recover(); r != nil {
		Logger(functionName, r)
		*ret = failClosed
	}
}

// ExportVoid is the counterpart of Export for void-returning exports.
func ExportVoid(functionName string) {
	if r := recover(); r != nil {
		Logger(functionName, r)
	}
}
