package abi

import (
	"testing"
	"unsafe"

	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

// The abi package references the Envoy symbols. This test never calls them, so
// the test binary needs only that the linker accepts the unresolved symbols.
// The Bazel target sets the linker flag for this. Under go test, Linux needs
// CGO_LDFLAGS=-Wl,--unresolved-symbols=ignore-all, and macOS already links with
// dynamic lookup.

type foreignHandle struct{ shared.HttpFilterHandle }

type foreignConfigHandle struct{ shared.HttpFilterConfigHandle }

func TestHttpFilterEnvoyPointer(t *testing.T) {
	var marker byte
	want := unsafe.Pointer(&marker)
	h := &dymHttpFilterHandle{}
	// The field has a cgo type, so the test sets it through its address.
	*(*unsafe.Pointer)(unsafe.Pointer(&h.hostPluginPtr)) = want
	got, ok := HttpFilterEnvoyPointer(h)
	if !ok || got != want {
		t.Fatalf("got %p, %v; want %p, true", got, ok, want)
	}
	for name, fh := range map[string]shared.HttpFilterHandle{
		"foreign":   foreignHandle{},
		"typed nil": (*dymHttpFilterHandle)(nil),
		"nil":       nil,
	} {
		if p, ok := HttpFilterEnvoyPointer(fh); ok || p != nil {
			t.Errorf("%s: got %p, %v; want nil, false", name, p, ok)
		}
	}
}

func TestHttpFilterConfigEnvoyPointer(t *testing.T) {
	var marker byte
	want := unsafe.Pointer(&marker)
	h := &dymConfigHandle{}
	*(*unsafe.Pointer)(unsafe.Pointer(&h.hostConfigPtr)) = want
	got, ok := HttpFilterConfigEnvoyPointer(h)
	if !ok || got != want {
		t.Fatalf("got %p, %v; want %p, true", got, ok, want)
	}
	for name, ch := range map[string]shared.HttpFilterConfigHandle{
		"foreign":   foreignConfigHandle{},
		"typed nil": (*dymConfigHandle)(nil),
		"nil":       nil,
	} {
		if p, ok := HttpFilterConfigEnvoyPointer(ch); ok || p != nil {
			t.Errorf("%s: got %p, %v; want nil, false", name, p, ok)
		}
	}
}
