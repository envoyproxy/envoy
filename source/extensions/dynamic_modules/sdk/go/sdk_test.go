package sdk

import (
	"sync/atomic"
	"testing"
	"unsafe"

	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

// The factory registries in sdk.go are package-level globals. Tests in this file pick
// per-test unique factory names so concurrent registration in other tests can't collide.
//
// Each registry is exercised through three behaviours:
//  1. Register-then-Get round-trips correctly.
//  2. Get returns nil when the name is unknown.
//  3. Re-registering the same name panics.
//
// We also cover the New* constructors that go through the registry plus an error path,
// the program-handle indirection (default noop, replacement, Log), and the package-level
// program utilities (RegisterFunction/GetFunction, RegisterSharedData/GetSharedData).

// -----------------------------------------------------------------------------
// Test factory implementations — minimal, name them explicitly per registry so
// tests don't accidentally cross-register.
// -----------------------------------------------------------------------------

type fakeHttpFilterConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}
type fakeNetworkFilterConfigFactory struct {
	shared.EmptyNetworkFilterConfigFactory
}
type fakeListenerFilterConfigFactory struct {
	shared.EmptyListenerFilterConfigFactory
}
type fakeUdpListenerFilterConfigFactory struct {
	shared.EmptyUdpListenerFilterConfigFactory
}
type fakeAccessLoggerConfigFactory struct {
	shared.EmptyAccessLoggerConfigFactory
}
type fakeMatcherConfigFactory struct {
	shared.EmptyMatcherConfigFactory
}
type fakeCertValidatorConfigFactory struct {
	shared.EmptyCertValidatorConfigFactory
}
type fakeDnsResolverConfigFactory struct {
	shared.EmptyDnsResolverConfigFactory
}
type fakeUpstreamHttpTcpBridgeConfigFactory struct {
	shared.EmptyUpstreamHttpTcpBridgeConfigFactory
}
type fakeTracerConfigFactory struct {
	shared.EmptyTracerConfigFactory
}
type fakeTransportSocketFactoryConfigFactory struct {
	shared.EmptyTransportSocketFactoryConfigFactory
}
type fakeLoadBalancerConfigFactory struct {
	shared.EmptyLoadBalancerConfigFactory
}
type fakeBootstrapExtensionConfigFactory struct {
	shared.EmptyBootstrapExtensionConfigFactory
}
type fakeClusterConfigFactory struct {
	shared.EmptyClusterConfigFactory
}

// -----------------------------------------------------------------------------
// HTTP filter registry
// -----------------------------------------------------------------------------

func TestHttpFilterRegistry(t *testing.T) {
	const name = "test_http_filter_registry_filter"
	f := &fakeHttpFilterConfigFactory{}
	RegisterHttpFilterConfigFactories(map[string]shared.HttpFilterConfigFactory{name: f})

	if got := GetHttpFilterConfigFactory(name); got != f {
		t.Errorf("Get returned %v, want %v", got, f)
	}
	if got := GetHttpFilterConfigFactory("does_not_exist"); got != nil {
		t.Errorf("Get for unknown name returned %v, want nil", got)
	}

	mustPanic(t, "duplicate http filter registration", func() {
		RegisterHttpFilterConfigFactories(map[string]shared.HttpFilterConfigFactory{name: f})
	})
}

func TestNewHttpFilterFactory(t *testing.T) {
	const name = "test_new_http_filter_factory"
	f := &fakeHttpFilterConfigFactory{}
	RegisterHttpFilterConfigFactories(map[string]shared.HttpFilterConfigFactory{name: f})

	out, err := NewHttpFilterFactory(nil, name, nil)
	if err != nil {
		t.Fatalf("unexpected err: %v", err)
	}
	if out == nil {
		t.Fatal("NewHttpFilterFactory returned nil factory")
	}

	if _, err := NewHttpFilterFactory(nil, "does_not_exist", nil); err == nil {
		t.Error("NewHttpFilterFactory for unknown name should error")
	}
}

// -----------------------------------------------------------------------------
// Network filter registry
// -----------------------------------------------------------------------------

func TestNetworkFilterRegistry(t *testing.T) {
	const name = "test_network_filter_registry_filter"
	f := &fakeNetworkFilterConfigFactory{}
	RegisterNetworkFilterConfigFactories(map[string]shared.NetworkFilterConfigFactory{name: f})

	if got := GetNetworkFilterConfigFactory(name); got != f {
		t.Errorf("Get returned %v, want %v", got, f)
	}
	if got := GetNetworkFilterConfigFactory("does_not_exist"); got != nil {
		t.Errorf("Get for unknown name returned %v, want nil", got)
	}

	mustPanic(t, "duplicate network filter registration", func() {
		RegisterNetworkFilterConfigFactories(map[string]shared.NetworkFilterConfigFactory{name: f})
	})
}

func TestNewNetworkFilterFactory(t *testing.T) {
	const name = "test_new_network_filter_factory"
	f := &fakeNetworkFilterConfigFactory{}
	RegisterNetworkFilterConfigFactories(map[string]shared.NetworkFilterConfigFactory{name: f})

	out, err := NewNetworkFilterFactory(nil, name, nil)
	if err != nil {
		t.Fatalf("unexpected err: %v", err)
	}
	if out == nil {
		t.Fatal("NewNetworkFilterFactory returned nil factory")
	}

	if _, err := NewNetworkFilterFactory(nil, "does_not_exist", nil); err == nil {
		t.Error("NewNetworkFilterFactory for unknown name should error")
	}
}

// -----------------------------------------------------------------------------
// Remaining 12 registries: same shape — register, get, panic on duplicate.
// -----------------------------------------------------------------------------

func TestListenerFilterRegistry(t *testing.T) {
	const name = "test_listener_filter_registry"
	f := &fakeListenerFilterConfigFactory{}
	RegisterListenerFilterConfigFactories(map[string]shared.ListenerFilterConfigFactory{name: f})
	if got := GetListenerFilterConfigFactory(name); got != f {
		t.Errorf("Get returned %v, want %v", got, f)
	}
	if got := GetListenerFilterConfigFactory("does_not_exist"); got != nil {
		t.Errorf("Get for unknown name returned %v, want nil", got)
	}
	mustPanic(t, "duplicate listener filter registration", func() {
		RegisterListenerFilterConfigFactories(map[string]shared.ListenerFilterConfigFactory{name: f})
	})
}

func TestUdpListenerFilterRegistry(t *testing.T) {
	const name = "test_udp_listener_filter_registry"
	f := &fakeUdpListenerFilterConfigFactory{}
	RegisterUdpListenerFilterConfigFactories(map[string]shared.UdpListenerFilterConfigFactory{name: f})
	if got := GetUdpListenerFilterConfigFactory(name); got != f {
		t.Errorf("Get returned %v, want %v", got, f)
	}
	if got := GetUdpListenerFilterConfigFactory("does_not_exist"); got != nil {
		t.Errorf("Get for unknown name returned %v, want nil", got)
	}
	mustPanic(t, "duplicate UDP listener filter registration", func() {
		RegisterUdpListenerFilterConfigFactories(map[string]shared.UdpListenerFilterConfigFactory{name: f})
	})
}

func TestAccessLoggerRegistry(t *testing.T) {
	const name = "test_access_logger_registry"
	f := &fakeAccessLoggerConfigFactory{}
	RegisterAccessLoggerConfigFactories(map[string]shared.AccessLoggerConfigFactory{name: f})
	if got := GetAccessLoggerConfigFactory(name); got != f {
		t.Errorf("Get returned %v, want %v", got, f)
	}
	if got := GetAccessLoggerConfigFactory("does_not_exist"); got != nil {
		t.Errorf("Get for unknown name returned %v, want nil", got)
	}
	mustPanic(t, "duplicate access logger registration", func() {
		RegisterAccessLoggerConfigFactories(map[string]shared.AccessLoggerConfigFactory{name: f})
	})
}

func TestMatcherRegistry(t *testing.T) {
	const name = "test_matcher_registry"
	f := &fakeMatcherConfigFactory{}
	RegisterMatcherConfigFactories(map[string]shared.MatcherConfigFactory{name: f})
	if got := GetMatcherConfigFactory(name); got != f {
		t.Errorf("Get returned %v, want %v", got, f)
	}
	if got := GetMatcherConfigFactory("does_not_exist"); got != nil {
		t.Errorf("Get for unknown name returned %v, want nil", got)
	}
	mustPanic(t, "duplicate matcher registration", func() {
		RegisterMatcherConfigFactories(map[string]shared.MatcherConfigFactory{name: f})
	})
}

func TestCertValidatorRegistry(t *testing.T) {
	const name = "test_cert_validator_registry"
	f := &fakeCertValidatorConfigFactory{}
	RegisterCertValidatorConfigFactories(map[string]shared.CertValidatorConfigFactory{name: f})
	if got := GetCertValidatorConfigFactory(name); got != f {
		t.Errorf("Get returned %v, want %v", got, f)
	}
	if got := GetCertValidatorConfigFactory("does_not_exist"); got != nil {
		t.Errorf("Get for unknown name returned %v, want nil", got)
	}
	mustPanic(t, "duplicate cert validator registration", func() {
		RegisterCertValidatorConfigFactories(map[string]shared.CertValidatorConfigFactory{name: f})
	})
}

func TestDnsResolverRegistry(t *testing.T) {
	const name = "test_dns_resolver_registry"
	f := &fakeDnsResolverConfigFactory{}
	RegisterDnsResolverConfigFactories(map[string]shared.DnsResolverConfigFactory{name: f})
	if got := GetDnsResolverConfigFactory(name); got != f {
		t.Errorf("Get returned %v, want %v", got, f)
	}
	if got := GetDnsResolverConfigFactory("does_not_exist"); got != nil {
		t.Errorf("Get for unknown name returned %v, want nil", got)
	}
	mustPanic(t, "duplicate DNS resolver registration", func() {
		RegisterDnsResolverConfigFactories(map[string]shared.DnsResolverConfigFactory{name: f})
	})
}

func TestUpstreamHttpTcpBridgeRegistry(t *testing.T) {
	const name = "test_upstream_http_tcp_bridge_registry"
	f := &fakeUpstreamHttpTcpBridgeConfigFactory{}
	RegisterUpstreamHttpTcpBridgeConfigFactories(map[string]shared.UpstreamHttpTcpBridgeConfigFactory{name: f})
	if got := GetUpstreamHttpTcpBridgeConfigFactory(name); got != f {
		t.Errorf("Get returned %v, want %v", got, f)
	}
	if got := GetUpstreamHttpTcpBridgeConfigFactory("does_not_exist"); got != nil {
		t.Errorf("Get for unknown name returned %v, want nil", got)
	}
	mustPanic(t, "duplicate upstream bridge registration", func() {
		RegisterUpstreamHttpTcpBridgeConfigFactories(map[string]shared.UpstreamHttpTcpBridgeConfigFactory{name: f})
	})
}

func TestTracerRegistry(t *testing.T) {
	const name = "test_tracer_registry"
	f := &fakeTracerConfigFactory{}
	RegisterTracerConfigFactories(map[string]shared.TracerConfigFactory{name: f})
	if got := GetTracerConfigFactory(name); got != f {
		t.Errorf("Get returned %v, want %v", got, f)
	}
	if got := GetTracerConfigFactory("does_not_exist"); got != nil {
		t.Errorf("Get for unknown name returned %v, want nil", got)
	}
	mustPanic(t, "duplicate tracer registration", func() {
		RegisterTracerConfigFactories(map[string]shared.TracerConfigFactory{name: f})
	})
}

func TestTransportSocketFactoryRegistry(t *testing.T) {
	const name = "test_transport_socket_registry"
	f := &fakeTransportSocketFactoryConfigFactory{}
	RegisterTransportSocketFactoryConfigFactories(map[string]shared.TransportSocketFactoryConfigFactory{name: f})
	if got := GetTransportSocketFactoryConfigFactory(name); got != f {
		t.Errorf("Get returned %v, want %v", got, f)
	}
	if got := GetTransportSocketFactoryConfigFactory("does_not_exist"); got != nil {
		t.Errorf("Get for unknown name returned %v, want nil", got)
	}
	mustPanic(t, "duplicate transport socket registration", func() {
		RegisterTransportSocketFactoryConfigFactories(map[string]shared.TransportSocketFactoryConfigFactory{name: f})
	})
}

func TestLoadBalancerRegistry(t *testing.T) {
	const name = "test_load_balancer_registry"
	f := &fakeLoadBalancerConfigFactory{}
	RegisterLoadBalancerConfigFactories(map[string]shared.LoadBalancerConfigFactory{name: f})
	if got := GetLoadBalancerConfigFactory(name); got != f {
		t.Errorf("Get returned %v, want %v", got, f)
	}
	if got := GetLoadBalancerConfigFactory("does_not_exist"); got != nil {
		t.Errorf("Get for unknown name returned %v, want nil", got)
	}
	mustPanic(t, "duplicate load balancer registration", func() {
		RegisterLoadBalancerConfigFactories(map[string]shared.LoadBalancerConfigFactory{name: f})
	})
}

func TestBootstrapExtensionRegistry(t *testing.T) {
	const name = "test_bootstrap_extension_registry"
	f := &fakeBootstrapExtensionConfigFactory{}
	RegisterBootstrapExtensionConfigFactories(map[string]shared.BootstrapExtensionConfigFactory{name: f})
	if got := GetBootstrapExtensionConfigFactory(name); got != f {
		t.Errorf("Get returned %v, want %v", got, f)
	}
	if got := GetBootstrapExtensionConfigFactory("does_not_exist"); got != nil {
		t.Errorf("Get for unknown name returned %v, want nil", got)
	}
	mustPanic(t, "duplicate bootstrap extension registration", func() {
		RegisterBootstrapExtensionConfigFactories(map[string]shared.BootstrapExtensionConfigFactory{name: f})
	})
}

func TestClusterRegistry(t *testing.T) {
	const name = "test_cluster_registry"
	f := &fakeClusterConfigFactory{}
	RegisterClusterConfigFactories(map[string]shared.ClusterConfigFactory{name: f})
	if got := GetClusterConfigFactory(name); got != f {
		t.Errorf("Get returned %v, want %v", got, f)
	}
	if got := GetClusterConfigFactory("does_not_exist"); got != nil {
		t.Errorf("Get for unknown name returned %v, want nil", got)
	}
	mustPanic(t, "duplicate cluster registration", func() {
		RegisterClusterConfigFactories(map[string]shared.ClusterConfigFactory{name: f})
	})
}

// -----------------------------------------------------------------------------
// Program handle indirection
// -----------------------------------------------------------------------------

// fakeProgramHandle is a recording shared.ProgramHandle. Each method writes its inputs to
// fields that the test inspects after the call.
type fakeProgramHandle struct {
	concurrency       uint32
	validation        bool
	functions         map[string]unsafe.Pointer
	sharedData        map[string]unsafe.Pointer
	registerFnFails   bool
	registerDataFails bool

	logs []logRecord
}

type logRecord struct {
	level  shared.LogLevel
	format string
	args   []any
}

func (f *fakeProgramHandle) GetConcurrency() uint32 { return f.concurrency }
func (f *fakeProgramHandle) IsValidationMode() bool { return f.validation }
func (f *fakeProgramHandle) Log(l shared.LogLevel, format string, args ...any) {
	f.logs = append(f.logs, logRecord{l, format, args})
}
func (f *fakeProgramHandle) RegisterFunction(key string, ptr unsafe.Pointer) bool {
	if f.registerFnFails {
		return false
	}
	if f.functions == nil {
		f.functions = map[string]unsafe.Pointer{}
	}
	f.functions[key] = ptr
	return true
}
func (f *fakeProgramHandle) GetFunction(key string) (unsafe.Pointer, bool) {
	p, ok := f.functions[key]
	return p, ok
}
func (f *fakeProgramHandle) RegisterSharedData(key string, ptr unsafe.Pointer) bool {
	if f.registerDataFails {
		return false
	}
	if f.sharedData == nil {
		f.sharedData = map[string]unsafe.Pointer{}
	}
	f.sharedData[key] = ptr
	return true
}
func (f *fakeProgramHandle) GetSharedData(key string) (unsafe.Pointer, bool) {
	p, ok := f.sharedData[key]
	return p, ok
}

// withProgramHandle swaps in a ProgramHandle for the duration of fn and restores the
// previous one on exit. Tests must use this so they don't leak handle state into other
// tests in the package.
func withProgramHandle(t *testing.T, h shared.ProgramHandle, fn func()) {
	t.Helper()
	prev := *programHandle.Load()
	SetProgramHandle(h)
	defer SetProgramHandle(prev)
	fn()
}

func TestProgramHandle_DefaultIsNoop(t *testing.T) {
	// The package-level init() installs noopProgramHandle. Each accessor must return its
	// documented zero value so unconfigured modules don't blow up.
	noop := *programHandle.Load()
	if _, ok := noop.(*noopProgramHandle); !ok {
		t.Fatalf("default handle is %T, want *noopProgramHandle", noop)
	}

	if got := GetConcurrency(); got != 0 {
		t.Errorf("GetConcurrency() = %d, want 0", got)
	}
	if IsValidationMode() {
		t.Error("IsValidationMode() = true, want false")
	}
	var sentinel int
	ptr := unsafe.Pointer(&sentinel)
	if RegisterFunction("k", ptr) {
		t.Error("noop RegisterFunction returned true")
	}
	if p, ok := GetFunction("k"); p != nil || ok {
		t.Errorf("noop GetFunction = (%v, %v), want (nil, false)", p, ok)
	}
	if RegisterSharedData("k", ptr) {
		t.Error("noop RegisterSharedData returned true")
	}
	if p, ok := GetSharedData("k"); p != nil || ok {
		t.Errorf("noop GetSharedData = (%v, %v), want (nil, false)", p, ok)
	}
	// noop Log should not panic.
	Log(shared.LogLevelInfo, "noop %s", "ok")
}

func TestProgramHandle_Replacement(t *testing.T) {
	fake := &fakeProgramHandle{concurrency: 7, validation: true}
	withProgramHandle(t, fake, func() {
		if got := GetConcurrency(); got != 7 {
			t.Errorf("GetConcurrency() = %d, want 7", got)
		}
		if !IsValidationMode() {
			t.Error("IsValidationMode() = false, want true")
		}
	})

	// After deferred restore the noop is back.
	if got := GetConcurrency(); got != 0 {
		t.Errorf("after restore GetConcurrency() = %d, want 0", got)
	}
}

func TestProgramHandle_Log(t *testing.T) {
	fake := &fakeProgramHandle{}
	withProgramHandle(t, fake, func() {
		Log(shared.LogLevelWarn, "hello %s", "world")
		Log(shared.LogLevelError, "boom")
	})

	if got, want := len(fake.logs), 2; got != want {
		t.Fatalf("len(logs) = %d, want %d", got, want)
	}
	if fake.logs[0].level != shared.LogLevelWarn {
		t.Errorf("log[0].level = %v, want Warn", fake.logs[0].level)
	}
	if fake.logs[0].format != "hello %s" {
		t.Errorf("log[0].format = %q, want %q", fake.logs[0].format, "hello %s")
	}
	if len(fake.logs[0].args) != 1 || fake.logs[0].args[0].(string) != "world" {
		t.Errorf("log[0].args = %v, want [world]", fake.logs[0].args)
	}
	if fake.logs[1].level != shared.LogLevelError {
		t.Errorf("log[1].level = %v, want Error", fake.logs[1].level)
	}
}

func TestProgramHandle_FunctionRegistry(t *testing.T) {
	fake := &fakeProgramHandle{}
	var sentinel int
	ptr := unsafe.Pointer(&sentinel)

	withProgramHandle(t, fake, func() {
		if !RegisterFunction("fn", ptr) {
			t.Fatal("RegisterFunction returned false")
		}
		got, ok := GetFunction("fn")
		if !ok {
			t.Fatal("GetFunction returned false for known key")
		}
		if got != ptr {
			t.Errorf("GetFunction returned %v, want %v", got, ptr)
		}
		if _, ok := GetFunction("missing"); ok {
			t.Error("GetFunction returned true for unknown key")
		}
	})
}

func TestProgramHandle_SharedDataRegistry(t *testing.T) {
	fake := &fakeProgramHandle{}
	var sentinel int
	ptr := unsafe.Pointer(&sentinel)

	withProgramHandle(t, fake, func() {
		if !RegisterSharedData("data", ptr) {
			t.Fatal("RegisterSharedData returned false")
		}
		got, ok := GetSharedData("data")
		if !ok {
			t.Fatal("GetSharedData returned false for known key")
		}
		if got != ptr {
			t.Errorf("GetSharedData returned %v, want %v", got, ptr)
		}
		if _, ok := GetSharedData("missing"); ok {
			t.Error("GetSharedData returned true for unknown key")
		}
	})
}

func TestProgramHandle_AtomicSwap(t *testing.T) {
	// SetProgramHandle uses atomic.Pointer, so concurrent reads while a swap is in
	// progress must return either the old or new handle — never a torn pointer. We can't
	// detect tearing in a unit test directly, but we can at least sanity-check that a
	// swap is observed by subsequent reads on a different goroutine.
	first := &fakeProgramHandle{concurrency: 1}
	second := &fakeProgramHandle{concurrency: 2}

	prev := *programHandle.Load()
	defer SetProgramHandle(prev)

	SetProgramHandle(first)
	if got := GetConcurrency(); got != 1 {
		t.Fatalf("after first set: GetConcurrency = %d, want 1", got)
	}

	done := make(chan struct{})
	var observed atomic.Uint32
	go func() {
		observed.Store(GetConcurrency())
		close(done)
	}()
	SetProgramHandle(second)
	<-done

	// observed must be 1 or 2 — either is fine (race over the swap), but we never want 0
	// (which would indicate the noop default leaked back in).
	if v := observed.Load(); v != 1 && v != 2 {
		t.Errorf("observed concurrency = %d, want 1 or 2", v)
	}
	if got := GetConcurrency(); got != 2 {
		t.Errorf("after second set: GetConcurrency = %d, want 2", got)
	}
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// mustPanic asserts fn panics. It does not assert on the panic message — re-registration
// panics include the factory name which we don't want to lock down here.
func mustPanic(t *testing.T, label string, fn func()) {
	t.Helper()
	defer func() {
		if r := recover(); r == nil {
			t.Errorf("%s: expected panic, got none", label)
		}
	}()
	fn()
}
