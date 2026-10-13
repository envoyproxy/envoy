package abi

import (
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/internal/recovery"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

// init points the recovery barrier at the Envoy host logger, so a module panic recovered at a cgo
// export is reported at error level before the export returns its fail-closed value.
func init() {
	recovery.Logger = func(functionName string, recovered any) {
		hostLog(shared.LogLevelError,
			"%s: recovered panic at the ABI boundary: %v", []any{functionName, recovered})
	}
}
