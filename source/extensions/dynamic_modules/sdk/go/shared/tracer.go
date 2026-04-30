//go:generate mockgen -source=tracer.go -destination=mocks/mock_tracer.go -package=mocks
package shared

// Tracer SDK surface for dynamic modules.
//
// Mirrors the Rust SDK's `tracer` module. This extension implements the Tracing::Driver and
// Tracing::Span interfaces, allowing modules to create spans, propagate trace context, set
// tags, log events, and report traces to arbitrary backends.
//
// Lifecycle: Envoy calls TracerConfigFactory.Create exactly once per tracer configuration to
// obtain a Tracer. The Tracer's StartSpan is invoked at the beginning of each traceable
// request, returning a TracerSpan. The span receives SetOperation / SetTag / Log / Finish /
// InjectContext / SpawnChild / SetSampled / UseLocalDecision / GetBaggage / SetBaggage /
// GetTraceID / GetSpanID hooks, and finally OnDestroy.
//
// Trace-context callbacks (Get/Set/RemoveTraceContextValue and the Get*ContextProtocol/Host/
// Path/Method getters) are made available via TracerSpanContext, which is passed both to
// StartSpan (reading incoming propagation headers) and to InjectContext (writing outgoing
// propagation headers).

// TraceReason corresponds to envoy_dynamic_module_type_trace_reason / Envoy's Tracing::Reason.
type TraceReason uint32

const (
	// TraceReasonNotTraceable — the request is not traceable.
	TraceReasonNotTraceable TraceReason = iota
	// TraceReasonHealthCheck — the request is a health check.
	TraceReasonHealthCheck
	// TraceReasonSampling — the request was selected by sampling.
	TraceReasonSampling
	// TraceReasonServiceForced — the service forced tracing.
	TraceReasonServiceForced
	// TraceReasonClientForced — the client forced tracing (e.g., x-client-trace-id).
	TraceReasonClientForced
)

// Tracer is the module-side tracer object. A single instance is created per tracer
// configuration and is invoked for every traceable request. Implementations must be safe for
// concurrent calls.
type Tracer interface {
	// StartSpan is called when a new span needs to be started for an incoming request. The ctx
	// gives access to the incoming trace-context headers. operationName is the span's
	// operation name; traced reflects Envoy's sampling decision; reason gives the source of
	// that decision.
	//
	// Returning nil causes Envoy to fall back to a NullSpan.
	StartSpan(ctx TracerSpanContext, operationName string, traced bool, reason TraceReason) TracerSpan

	// OnDestroy is called when the tracer configuration is destroyed.
	OnDestroy()
}

// EmptyTracer is a no-op Tracer that returns nil from StartSpan.
type EmptyTracer struct{}

func (*EmptyTracer) StartSpan(_ TracerSpanContext, _ string, _ bool, _ TraceReason) TracerSpan {
	return nil
}
func (*EmptyTracer) OnDestroy() {}

// TracerConfigFactory is the top-level factory the module registers via
// sdk.RegisterTracerConfigFactories.
type TracerConfigFactory interface {
	// Create parses unparsedConfig and returns a Tracer. The handle is valid for the lifetime
	// of the Tracer and provides metric definitions.
	Create(handle TracerConfigHandle, unparsedConfig []byte) (Tracer, error)
}

// EmptyTracerConfigFactory is a no-op TracerConfigFactory.
type EmptyTracerConfigFactory struct{}

func (*EmptyTracerConfigFactory) Create(_ TracerConfigHandle, _ []byte) (Tracer, error) {
	return &EmptyTracer{}, nil
}

// TracerConfigHandle is the config-context handle. It supports labeled metrics in the same
// style as DnsResolverConfigHandle: label values passed at increment-time MUST match the order
// and length of label names declared at definition-time.
type TracerConfigHandle interface {
	// DefineCounter creates a per-config counter template with the given label names.
	DefineCounter(name string, labelNames []string) (MetricID, MetricsResult)

	// IncrementCounter increments a counter by value.
	IncrementCounter(id MetricID, labelValues []string, value uint64) MetricsResult

	// DefineGauge creates a per-config gauge template with the given label names.
	DefineGauge(name string, labelNames []string) (MetricID, MetricsResult)

	// SetGauge sets a gauge to value.
	SetGauge(id MetricID, labelValues []string, value uint64) MetricsResult

	// DefineHistogram creates a per-config histogram template with the given label names.
	DefineHistogram(name string, labelNames []string) (MetricID, MetricsResult)

	// RecordHistogramValue records value in a histogram.
	RecordHistogramValue(id MetricID, labelValues []string, value uint64) MetricsResult
}

// TracerSpan is the module-side span object. It is created by Tracer.StartSpan or
// TracerSpan.SpawnChild and lives until Finish (followed by OnDestroy).
//
// All methods on a single TracerSpan are called on the same thread; the module does not need
// internal synchronization for them. However, the module MAY be holding the span across
// threads (e.g., to write it to a backend on a different goroutine) — that is up to the
// module.
type TracerSpan interface {
	// SetOperation updates the span's operation name.
	SetOperation(operation string)

	// SetTag sets a tag on the span.
	SetTag(key, value string)

	// Log records a log event on the span at the given timestamp (nanoseconds since epoch).
	Log(timestampNs int64, event string)

	// Finish finishes and reports the span. After Finish, Envoy will call OnDestroy.
	Finish()

	// InjectContext is called when Envoy needs to propagate trace context to an upstream
	// request. The module should use ctx.SetTraceContextValue / RemoveTraceContextValue to
	// write propagation headers on the outgoing request.
	InjectContext(ctx TracerSpanContext)

	// SpawnChild creates a child span with the given operation name and start time
	// (nanoseconds since epoch). Returning nil falls back to a NullSpan for the child.
	SpawnChild(operationName string, startTimeNs int64) TracerSpan

	// SetSampled overrides the sampling decision for this span. If false, the span will not
	// be reported.
	SetSampled(sampled bool)

	// UseLocalDecision reports whether the span uses Envoy's local sampling decision
	// (returning true) or its own (false).
	UseLocalDecision() bool

	// GetBaggage retrieves a baggage value by key. Returns the value and true if found.
	//
	// Implementations should return memory that remains valid until the span is destroyed —
	// the runtime hands the bytes directly to Envoy without copying.
	GetBaggage(key string) ([]byte, bool)

	// SetBaggage sets a baggage key/value pair on the span.
	SetBaggage(key, value string)

	// GetTraceID retrieves the trace ID. Implementations should return memory that remains
	// valid until the span is destroyed.
	GetTraceID() ([]byte, bool)

	// GetSpanID retrieves the span ID. Implementations should return memory that remains
	// valid until the span is destroyed.
	GetSpanID() ([]byte, bool)

	// OnDestroy is called when the span is being destroyed (after Finish).
	OnDestroy()
}

// EmptyTracerSpan is a no-op TracerSpan. SpawnChild returns nil; getters return (nil, false);
// UseLocalDecision returns true.
type EmptyTracerSpan struct{}

func (*EmptyTracerSpan) SetOperation(_ string)                   {}
func (*EmptyTracerSpan) SetTag(_, _ string)                      {}
func (*EmptyTracerSpan) Log(_ int64, _ string)                   {}
func (*EmptyTracerSpan) Finish()                                 {}
func (*EmptyTracerSpan) InjectContext(_ TracerSpanContext)       {}
func (*EmptyTracerSpan) SpawnChild(_ string, _ int64) TracerSpan { return nil }
func (*EmptyTracerSpan) SetSampled(_ bool)                       {}
func (*EmptyTracerSpan) UseLocalDecision() bool                  { return true }
func (*EmptyTracerSpan) GetBaggage(_ string) ([]byte, bool)      { return nil, false }
func (*EmptyTracerSpan) SetBaggage(_, _ string)                  {}
func (*EmptyTracerSpan) GetTraceID() ([]byte, bool)              { return nil, false }
func (*EmptyTracerSpan) GetSpanID() ([]byte, bool)               { return nil, false }
func (*EmptyTracerSpan) OnDestroy()                              {}

// TracerSpanContext is the per-call handle passed to Tracer.StartSpan (where it gives access
// to incoming trace-context headers) and to TracerSpan.InjectContext (where it lets the
// module write outgoing propagation headers).
//
// Methods on this handle are valid only for the duration of the StartSpan or InjectContext
// callback that received it; do not retain it.
type TracerSpanContext interface {
	// GetTraceContextValue retrieves a trace-context header value by key.
	//
	// NOTE: The buffer is owned by Envoy and only valid for the duration of the current
	// callback. Copy if you need to keep it.
	GetTraceContextValue(key string) (UnsafeEnvoyBuffer, bool)

	// SetTraceContextValue sets a trace-context header. Typically used during InjectContext to
	// write propagation headers on the outgoing request.
	SetTraceContextValue(key, value string)

	// RemoveTraceContextValue removes a trace-context header.
	RemoveTraceContextValue(key string)

	// GetTraceContextProtocol returns the protocol of the traceable stream
	// (e.g., "HTTP/1.1", "HTTP/2"). Returns false if not available.
	GetTraceContextProtocol() (UnsafeEnvoyBuffer, bool)

	// GetTraceContextHost returns the host of the traceable stream.
	GetTraceContextHost() (UnsafeEnvoyBuffer, bool)

	// GetTraceContextPath returns the path of the traceable stream.
	GetTraceContextPath() (UnsafeEnvoyBuffer, bool)

	// GetTraceContextMethod returns the method of the traceable stream.
	GetTraceContextMethod() (UnsafeEnvoyBuffer, bool)
}
