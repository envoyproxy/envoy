package fake

import (
	"strings"
	"unsafe"

	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

var _ shared.AccessLogContext = (*FakeAccessLogContext)(nil)

// FakeAccessLogContext is an in-memory implementation of shared.AccessLogContext for tests.
//
// All fields are public so tests can populate exactly the data their access logger reads.
// Any field left at its zero value behaves like "not set" — getters return an empty
// UnsafeEnvoyBuffer and false, getters that return only a value return zero.
//
// Headers are case-insensitive. Use NewFakeAccessLogContext to populate them; direct
// assignment to the maps is also supported but expects already-lowercased keys.
type FakeAccessLogContext struct {
	// Headers indexed by HttpHeaderType. Each entry is a slice of [name, value] pairs in
	// insertion order; the same name may appear multiple times.
	Headers map[shared.HttpHeaderType][][2]string

	// Attribute values keyed by AttributeID. Only the type-appropriate map is consulted by the
	// matching Get*. Tests should populate the right map for the AttributeID.
	StringAttributes map[shared.AttributeID]string
	NumberAttributes map[shared.AttributeID]uint64
	BoolAttributes   map[shared.AttributeID]bool

	// Response flag bitmask. Bit i corresponds to ResponseFlag(i).
	ResponseFlags uint64

	TimingInfo shared.AccessLogTimingInfo
	BytesInfo  shared.AccessLogBytesInfo

	// ---- addresses (port = 0 + ok = false when address is empty) ----
	DownstreamRemoteAddress       string
	DownstreamRemotePort          uint32
	DownstreamLocalAddress        string
	DownstreamLocalPort           uint32
	DownstreamDirectRemoteAddress string
	DownstreamDirectRemotePort    uint32
	DownstreamDirectLocalAddress  string
	DownstreamDirectLocalPort     uint32
	UpstreamRemoteAddress         string
	UpstreamRemotePort            uint32
	UpstreamLocalAddress          string
	UpstreamLocalPort             uint32

	// ---- upstream ----
	UpstreamCluster      string
	UpstreamHost         string
	UpstreamConnectionID uint64
	UpstreamTLSCipher    string
	UpstreamTLSSessionID string
	UpstreamPeerIssuer   string

	UpstreamPeerCertValidityStart int64
	UpstreamPeerCertValidityEnd   int64

	UpstreamPeerURISans  []string
	UpstreamLocalURISans []string
	UpstreamPeerDNSSans  []string
	UpstreamLocalDNSSans []string

	// ---- downstream TLS ----
	DownstreamTLSCipher        string
	DownstreamTLSSessionID     string
	DownstreamPeerIssuer       string
	DownstreamPeerSerial       string
	DownstreamPeerFingerprint1 string

	DownstreamPeerCertPresented     bool
	DownstreamPeerCertValidated     bool
	DownstreamPeerCertValidityStart int64
	DownstreamPeerCertValidityEnd   int64

	DownstreamPeerURISans  []string
	DownstreamLocalURISans []string
	DownstreamPeerDNSSans  []string
	DownstreamLocalDNSSans []string

	// ---- metadata / filter state / tracing ----
	// DynamicMetadata is keyed by "<filterName>/<path>" (no leading or trailing slash).
	DynamicMetadata map[string]string
	FilterState     map[string]string

	LocalReplyBody string
	TraceID        string
	SpanID         string
	TraceSampled   bool

	JA3Hash string
	JA4Hash string

	RequestHeadersBytes        uint64
	ResponseHeadersBytes       uint64
	ResponseTrailersBytes      uint64
	UpstreamProtocol           string
	UpstreamPoolReadyDurationNs int64

	WorkerIndex uint32
}

// NewFakeAccessLogContext returns a FakeAccessLogContext with empty (but non-nil) maps so
// tests can call Set* helpers on the returned value without nil-checking.
func NewFakeAccessLogContext() *FakeAccessLogContext {
	return &FakeAccessLogContext{
		Headers:          make(map[shared.HttpHeaderType][][2]string),
		StringAttributes: make(map[shared.AttributeID]string),
		NumberAttributes: make(map[shared.AttributeID]uint64),
		BoolAttributes:   make(map[shared.AttributeID]bool),
		DynamicMetadata:  make(map[string]string),
		FilterState:      make(map[string]string),
	}
}

// AddHeader appends a header to the given header map. Names are normalized to lowercase.
func (c *FakeAccessLogContext) AddHeader(headerType shared.HttpHeaderType, name, value string) {
	c.Headers[headerType] = append(c.Headers[headerType], [2]string{strings.ToLower(name), value})
}

// ---- headers ----

func (c *FakeAccessLogContext) GetHeadersSize(headerType shared.HttpHeaderType) uint64 {
	return uint64(len(c.Headers[headerType]))
}

func (c *FakeAccessLogContext) GetHeaders(headerType shared.HttpHeaderType) [][2]shared.UnsafeEnvoyBuffer {
	hs := c.Headers[headerType]
	out := make([][2]shared.UnsafeEnvoyBuffer, len(hs))
	for i, kv := range hs {
		out[i] = [2]shared.UnsafeEnvoyBuffer{stringBuf(kv[0]), stringBuf(kv[1])}
	}
	return out
}

func (c *FakeAccessLogContext) GetHeaderValue(
	headerType shared.HttpHeaderType, key string, index uint64,
) (shared.UnsafeEnvoyBuffer, uint64, bool) {
	lower := strings.ToLower(key)
	var matched []string
	for _, kv := range c.Headers[headerType] {
		if kv[0] == lower {
			matched = append(matched, kv[1])
		}
	}
	total := uint64(len(matched))
	if total == 0 || index >= total {
		return shared.UnsafeEnvoyBuffer{}, total, false
	}
	return stringBuf(matched[index]), total, true
}

// ---- attribute accessors ----

func (c *FakeAccessLogContext) GetAttributeString(id shared.AttributeID) (shared.UnsafeEnvoyBuffer, bool) {
	v, ok := c.StringAttributes[id]
	if !ok {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return stringBuf(v), true
}

func (c *FakeAccessLogContext) GetAttributeNumber(id shared.AttributeID) (uint64, bool) {
	v, ok := c.NumberAttributes[id]
	return v, ok
}

func (c *FakeAccessLogContext) GetAttributeBool(id shared.AttributeID) (bool, bool) {
	v, ok := c.BoolAttributes[id]
	return v, ok
}

// ---- response flags / timing / bytes ----

func (c *FakeAccessLogContext) HasResponseFlag(flag shared.ResponseFlag) bool {
	return c.ResponseFlags&(1<<uint64(flag)) != 0
}

func (c *FakeAccessLogContext) GetResponseFlags() uint64 { return c.ResponseFlags }

func (c *FakeAccessLogContext) GetTimingInfo() shared.AccessLogTimingInfo { return c.TimingInfo }

func (c *FakeAccessLogContext) GetBytesInfo() shared.AccessLogBytesInfo { return c.BytesInfo }

// ---- addresses ----

func (c *FakeAccessLogContext) GetDownstreamRemoteAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	return addr(c.DownstreamRemoteAddress, c.DownstreamRemotePort)
}
func (c *FakeAccessLogContext) GetDownstreamLocalAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	return addr(c.DownstreamLocalAddress, c.DownstreamLocalPort)
}
func (c *FakeAccessLogContext) GetDownstreamDirectRemoteAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	return addr(c.DownstreamDirectRemoteAddress, c.DownstreamDirectRemotePort)
}
func (c *FakeAccessLogContext) GetDownstreamDirectLocalAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	return addr(c.DownstreamDirectLocalAddress, c.DownstreamDirectLocalPort)
}
func (c *FakeAccessLogContext) GetUpstreamRemoteAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	return addr(c.UpstreamRemoteAddress, c.UpstreamRemotePort)
}
func (c *FakeAccessLogContext) GetUpstreamLocalAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	return addr(c.UpstreamLocalAddress, c.UpstreamLocalPort)
}

// ---- upstream info ----

func (c *FakeAccessLogContext) GetUpstreamCluster() (shared.UnsafeEnvoyBuffer, bool) {
	return optBuf(c.UpstreamCluster)
}
func (c *FakeAccessLogContext) GetUpstreamHost() (shared.UnsafeEnvoyBuffer, bool) {
	return optBuf(c.UpstreamHost)
}
func (c *FakeAccessLogContext) GetUpstreamConnectionID() uint64 { return c.UpstreamConnectionID }
func (c *FakeAccessLogContext) GetUpstreamTLSCipher() (shared.UnsafeEnvoyBuffer, bool) {
	return optBuf(c.UpstreamTLSCipher)
}
func (c *FakeAccessLogContext) GetUpstreamTLSSessionID() (shared.UnsafeEnvoyBuffer, bool) {
	return optBuf(c.UpstreamTLSSessionID)
}
func (c *FakeAccessLogContext) GetUpstreamPeerIssuer() (shared.UnsafeEnvoyBuffer, bool) {
	return optBuf(c.UpstreamPeerIssuer)
}
func (c *FakeAccessLogContext) GetUpstreamPeerCertValidityStart() int64 {
	return c.UpstreamPeerCertValidityStart
}
func (c *FakeAccessLogContext) GetUpstreamPeerCertValidityEnd() int64 {
	return c.UpstreamPeerCertValidityEnd
}
func (c *FakeAccessLogContext) GetUpstreamPeerURISans() []shared.UnsafeEnvoyBuffer {
	return strSliceToBufs(c.UpstreamPeerURISans)
}
func (c *FakeAccessLogContext) GetUpstreamLocalURISans() []shared.UnsafeEnvoyBuffer {
	return strSliceToBufs(c.UpstreamLocalURISans)
}
func (c *FakeAccessLogContext) GetUpstreamPeerDNSSans() []shared.UnsafeEnvoyBuffer {
	return strSliceToBufs(c.UpstreamPeerDNSSans)
}
func (c *FakeAccessLogContext) GetUpstreamLocalDNSSans() []shared.UnsafeEnvoyBuffer {
	return strSliceToBufs(c.UpstreamLocalDNSSans)
}

// ---- downstream connection / TLS info ----

func (c *FakeAccessLogContext) GetDownstreamTLSCipher() (shared.UnsafeEnvoyBuffer, bool) {
	return optBuf(c.DownstreamTLSCipher)
}
func (c *FakeAccessLogContext) GetDownstreamTLSSessionID() (shared.UnsafeEnvoyBuffer, bool) {
	return optBuf(c.DownstreamTLSSessionID)
}
func (c *FakeAccessLogContext) GetDownstreamPeerIssuer() (shared.UnsafeEnvoyBuffer, bool) {
	return optBuf(c.DownstreamPeerIssuer)
}
func (c *FakeAccessLogContext) GetDownstreamPeerSerial() (shared.UnsafeEnvoyBuffer, bool) {
	return optBuf(c.DownstreamPeerSerial)
}
func (c *FakeAccessLogContext) GetDownstreamPeerFingerprint1() (shared.UnsafeEnvoyBuffer, bool) {
	return optBuf(c.DownstreamPeerFingerprint1)
}
func (c *FakeAccessLogContext) GetDownstreamPeerCertPresented() bool {
	return c.DownstreamPeerCertPresented
}
func (c *FakeAccessLogContext) GetDownstreamPeerCertValidated() bool {
	return c.DownstreamPeerCertValidated
}
func (c *FakeAccessLogContext) GetDownstreamPeerCertValidityStart() int64 {
	return c.DownstreamPeerCertValidityStart
}
func (c *FakeAccessLogContext) GetDownstreamPeerCertValidityEnd() int64 {
	return c.DownstreamPeerCertValidityEnd
}
func (c *FakeAccessLogContext) GetDownstreamPeerURISans() []shared.UnsafeEnvoyBuffer {
	return strSliceToBufs(c.DownstreamPeerURISans)
}
func (c *FakeAccessLogContext) GetDownstreamLocalURISans() []shared.UnsafeEnvoyBuffer {
	return strSliceToBufs(c.DownstreamLocalURISans)
}
func (c *FakeAccessLogContext) GetDownstreamPeerDNSSans() []shared.UnsafeEnvoyBuffer {
	return strSliceToBufs(c.DownstreamPeerDNSSans)
}
func (c *FakeAccessLogContext) GetDownstreamLocalDNSSans() []shared.UnsafeEnvoyBuffer {
	return strSliceToBufs(c.DownstreamLocalDNSSans)
}

// ---- metadata / filter state / tracing ----

func (c *FakeAccessLogContext) GetDynamicMetadata(filterName, path string) (shared.UnsafeEnvoyBuffer, bool) {
	v, ok := c.DynamicMetadata[filterName+"/"+path]
	if !ok {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return stringBuf(v), true
}

func (c *FakeAccessLogContext) GetFilterState(key string) (shared.UnsafeEnvoyBuffer, bool) {
	v, ok := c.FilterState[key]
	if !ok {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return stringBuf(v), true
}

func (c *FakeAccessLogContext) GetLocalReplyBody() (shared.UnsafeEnvoyBuffer, bool) {
	return optBuf(c.LocalReplyBody)
}

func (c *FakeAccessLogContext) GetTraceID() (shared.UnsafeEnvoyBuffer, bool) { return optBuf(c.TraceID) }
func (c *FakeAccessLogContext) GetSpanID() (shared.UnsafeEnvoyBuffer, bool)  { return optBuf(c.SpanID) }
func (c *FakeAccessLogContext) IsTraceSampled() bool                         { return c.TraceSampled }

// ---- additional stream info ----

func (c *FakeAccessLogContext) GetJA3Hash() (shared.UnsafeEnvoyBuffer, bool) { return optBuf(c.JA3Hash) }
func (c *FakeAccessLogContext) GetJA4Hash() (shared.UnsafeEnvoyBuffer, bool) { return optBuf(c.JA4Hash) }

func (c *FakeAccessLogContext) GetRequestHeadersBytes() uint64   { return c.RequestHeadersBytes }
func (c *FakeAccessLogContext) GetResponseHeadersBytes() uint64  { return c.ResponseHeadersBytes }
func (c *FakeAccessLogContext) GetResponseTrailersBytes() uint64 { return c.ResponseTrailersBytes }
func (c *FakeAccessLogContext) GetUpstreamProtocol() (shared.UnsafeEnvoyBuffer, bool) {
	return optBuf(c.UpstreamProtocol)
}
func (c *FakeAccessLogContext) GetUpstreamPoolReadyDurationNs() int64 {
	return c.UpstreamPoolReadyDurationNs
}
func (c *FakeAccessLogContext) GetWorkerIndex() uint32 { return c.WorkerIndex }

// ---- helpers ----

func stringBuf(s string) shared.UnsafeEnvoyBuffer {
	if s == "" {
		return shared.UnsafeEnvoyBuffer{}
	}
	return shared.UnsafeEnvoyBuffer{Ptr: unsafe.StringData(s), Len: uint64(len(s))}
}

func optBuf(s string) (shared.UnsafeEnvoyBuffer, bool) {
	if s == "" {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return stringBuf(s), true
}

func addr(host string, port uint32) (shared.UnsafeEnvoyBuffer, uint32, bool) {
	if host == "" {
		return shared.UnsafeEnvoyBuffer{}, 0, false
	}
	return stringBuf(host), port, true
}

func strSliceToBufs(s []string) []shared.UnsafeEnvoyBuffer {
	if len(s) == 0 {
		return nil
	}
	out := make([]shared.UnsafeEnvoyBuffer, len(s))
	for i, v := range s {
		out[i] = stringBuf(v)
	}
	return out
}
