package fake

import (
	"unsafe"

	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

var (
	_ shared.Span      = (*FakeSpan)(nil)
	_ shared.ChildSpan = (*FakeChildSpan)(nil)
)

// FakeSpan is an in-memory implementation of shared.Span for tests. It records tags, baggage,
// log events, sampling decisions, and the tree of spawned child spans.
//
// FakeSpan is not safe for concurrent use; tracing in a single HTTP filter is single-threaded
// per stream.
type FakeSpan struct {
	Operation string
	Tags      map[string]string
	Baggage   map[string]string
	Logs      []string
	Sampled   bool
	TraceID   string
	SpanID    string
	// Children holds spans created via SpawnChild, in spawn order.
	Children []*FakeChildSpan
}

// NewFakeSpan creates a new FakeSpan with the given operation name. TraceID and SpanID are
// empty by default; set them on the returned struct if your test needs to read them back.
func NewFakeSpan(operation string) *FakeSpan {
	return &FakeSpan{
		Operation: operation,
		Tags:      make(map[string]string),
		Baggage:   make(map[string]string),
		Sampled:   true,
	}
}

func (s *FakeSpan) SetTag(key, value string)        { s.Tags[key] = value }
func (s *FakeSpan) SetOperation(operation string)   { s.Operation = operation }
func (s *FakeSpan) Log(event string)                { s.Logs = append(s.Logs, event) }
func (s *FakeSpan) SetSampled(sampled bool)         { s.Sampled = sampled }
func (s *FakeSpan) SetBaggage(key, value string)    { s.Baggage[key] = value }

func (s *FakeSpan) GetBaggage(key string) (shared.UnsafeEnvoyBuffer, bool) {
	v, ok := s.Baggage[key]
	if !ok {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return shared.UnsafeEnvoyBuffer{Ptr: unsafe.StringData(v), Len: uint64(len(v))}, true
}

func (s *FakeSpan) GetTraceID() (shared.UnsafeEnvoyBuffer, bool) {
	if s.TraceID == "" {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return shared.UnsafeEnvoyBuffer{Ptr: unsafe.StringData(s.TraceID), Len: uint64(len(s.TraceID))}, true
}

func (s *FakeSpan) GetSpanID() (shared.UnsafeEnvoyBuffer, bool) {
	if s.SpanID == "" {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return shared.UnsafeEnvoyBuffer{Ptr: unsafe.StringData(s.SpanID), Len: uint64(len(s.SpanID))}, true
}

// SpawnChild creates a new FakeChildSpan, appends it to s.Children, and returns it. Baggage
// from s is copied to the child so subsequent SetBaggage on the child does not mutate the
// parent.
func (s *FakeSpan) SpawnChild(operationName string) shared.ChildSpan {
	child := newFakeChildSpan(operationName, s.Baggage, s.Sampled)
	s.Children = append(s.Children, child)
	return child
}

// FakeChildSpan is an in-memory implementation of shared.ChildSpan for tests. It tracks the
// same state as FakeSpan plus a Finished flag.
type FakeChildSpan struct {
	Operation string
	Tags      map[string]string
	Baggage   map[string]string
	Logs      []string
	Sampled   bool
	Finished  bool
	Children  []*FakeChildSpan
}

func newFakeChildSpan(operation string, parentBaggage map[string]string, sampled bool) *FakeChildSpan {
	baggage := make(map[string]string, len(parentBaggage))
	for k, v := range parentBaggage {
		baggage[k] = v
	}
	return &FakeChildSpan{
		Operation: operation,
		Tags:      make(map[string]string),
		Baggage:   baggage,
		Sampled:   sampled,
	}
}

// NewFakeChildSpan creates a standalone FakeChildSpan for tests that exercise child-span
// behavior without a parent FakeSpan.
func NewFakeChildSpan(operation string) *FakeChildSpan {
	return newFakeChildSpan(operation, nil, true)
}

func (c *FakeChildSpan) SetTag(key, value string)      { c.Tags[key] = value }
func (c *FakeChildSpan) SetOperation(operation string) { c.Operation = operation }
func (c *FakeChildSpan) Log(event string)              { c.Logs = append(c.Logs, event) }
func (c *FakeChildSpan) SetSampled(sampled bool)       { c.Sampled = sampled }
func (c *FakeChildSpan) SetBaggage(key, value string)  { c.Baggage[key] = value }

// SpawnChild creates a child of this child span, propagating baggage and sampling.
func (c *FakeChildSpan) SpawnChild(operationName string) shared.ChildSpan {
	child := newFakeChildSpan(operationName, c.Baggage, c.Sampled)
	c.Children = append(c.Children, child)
	return child
}

// Finish marks this span finished. Subsequent calls are no-ops, matching the documented
// real-implementation contract.
func (c *FakeChildSpan) Finish() { c.Finished = true }
