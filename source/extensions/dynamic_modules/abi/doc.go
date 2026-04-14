// Package abi contains the C ABI header for Envoy dynamic modules.
//
// This file exists so that `go mod vendor` includes this directory.
// Without a .go file, the Go toolchain skips this directory during
// vendoring, which causes the abi.h header (needed by the Go SDK's
// cgo include) to be missing from the vendor tree.
package abi
