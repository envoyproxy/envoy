package fake

import (
	"reflect"
	"testing"
)

func TestFakeBodyBuffer_Drain(t *testing.T) {
	for _, tt := range []struct {
		name     string
		body     string
		drain    uint64
		expected string
	}{
		{"zero does nothing", `{"status":"active"}`, 0, `{"status":"active"}`},
		{"partial", `{"status":"active"}`, 10, `"active"}`},
		{"exact size", `{"status":"active"}`, 19, ""},
		{"exceeds size", `{"status":"active"}`, 100, ""},
		{"empty buffer", "", 0, ""},
	} {
		t.Run(tt.name, func(t *testing.T) {
			buf := NewFakeBodyBuffer([]byte(tt.body))
			buf.Drain(tt.drain)
			if got := string(buf.Body); got != tt.expected {
				t.Errorf("got %q, want %q", got, tt.expected)
			}
		})
	}
}

func TestFakeBodyBuffer_Append(t *testing.T) {
	for _, tt := range []struct {
		name     string
		body     string
		data     string
		expected string
	}{
		{"to non-empty", `{"name":"envoy"`, `,"version":"1.32"}`, `{"name":"envoy","version":"1.32"}`},
		{"to empty", "", `{"name":"envoy"}`, `{"name":"envoy"}`},
		{"empty data", `{"name":"envoy"}`, "", `{"name":"envoy"}`},
	} {
		t.Run(tt.name, func(t *testing.T) {
			buf := NewFakeBodyBuffer([]byte(tt.body))
			buf.Append([]byte(tt.data))
			if got := string(buf.Body); got != tt.expected {
				t.Errorf("got %q, want %q", got, tt.expected)
			}
		})
	}
}

func TestFakeBodyBuffer_GetSize(t *testing.T) {
	for _, tt := range []struct {
		name     string
		body     string
		expected uint64
	}{
		{"json body", `{"status":"active"}`, 19},
		{"empty body", "", 0},
	} {
		t.Run(tt.name, func(t *testing.T) {
			if got := NewFakeBodyBuffer([]byte(tt.body)).GetSize(); got != tt.expected {
				t.Errorf("got %d, want %d", got, tt.expected)
			}
		})
	}
}

func TestFakeBodyBuffer_GetChunks(t *testing.T) {
	for _, tt := range []struct {
		name     string
		body     string
		expected uint64
	}{
		{"json body", `{"status":"active"}`, 19},
		{"empty body", "", 0},
	} {
		t.Run(tt.name, func(t *testing.T) {
			if got := NewFakeBodyBuffer([]byte(tt.body)).GetChunks()[0].Len; got != tt.expected {
				t.Errorf("got %d, want %d", got, tt.expected)
			}
		})
	}
}

func TestFakeHeaderMap_Get(t *testing.T) {
	for _, tt := range []struct {
		name     string
		headers  map[string][]string
		key      string
		expected int
	}{
		{"single value", map[string][]string{"content-type": {"application/json"}}, "content-type", 1},
		{"multiple values", map[string][]string{"x-forwarded-for": {"10.0.0.1", "10.0.0.2"}}, "x-forwarded-for", 2},
		{"case insensitive", map[string][]string{"Content-Type": {"application/json"}}, "content-type", 1},
		{"missing key", map[string][]string{}, "authorization", 0},
	} {
		t.Run(tt.name, func(t *testing.T) {
			if got := len(NewFakeHeaderMap(tt.headers).Get(tt.key)); got != tt.expected {
				t.Errorf("got %d, want %d", got, tt.expected)
			}
		})
	}
}

func TestFakeHeaderMap_GetOne(t *testing.T) {
	for _, tt := range []struct {
		name     string
		headers  map[string][]string
		key      string
		expected uint64
	}{
		{"exists", map[string][]string{"content-type": {"application/json"}}, "content-type", 16},
		{"first of many", map[string][]string{"x-forwarded-for": {"10.0.0.1", "10.0.0.2"}}, "x-forwarded-for", 8},
		{"case insensitive", map[string][]string{"Content-Type": {"application/json"}}, "content-type", 16},
		{"missing", map[string][]string{}, "authorization", 0},
	} {
		t.Run(tt.name, func(t *testing.T) {
			if got := NewFakeHeaderMap(tt.headers).GetOne(tt.key).Len; got != tt.expected {
				t.Errorf("got %d, want %d", got, tt.expected)
			}
		})
	}
}

func TestFakeHeaderMap_GetAll(t *testing.T) {
	for _, tt := range []struct {
		name     string
		headers  map[string][]string
		expected int
	}{
		{"single header", map[string][]string{"content-type": {"application/json"}}, 1},
		{"multiple headers", map[string][]string{"content-type": {"application/json"}, "x-forwarded-for": {"10.0.0.1", "10.0.0.2"}}, 3},
		{"empty", map[string][]string{}, 0},
	} {
		t.Run(tt.name, func(t *testing.T) {
			if got := len(NewFakeHeaderMap(tt.headers).GetAll()); got != tt.expected {
				t.Errorf("got %d, want %d", got, tt.expected)
			}
		})
	}
}

func TestFakeHeaderMap_Set(t *testing.T) {
	for _, tt := range []struct {
		name     string
		initial  map[string][]string
		key      string
		value    string
		expected map[string][]string
	}{
		{
			"new header",
			map[string][]string{},
			"content-type", "application/json",
			map[string][]string{"content-type": {"application/json"}},
		},
		{
			"replaces existing",
			map[string][]string{"content-type": {"text/html", "text/plain"}},
			"content-type", "application/json",
			map[string][]string{"content-type": {"application/json"}},
		},
		{
			"case insensitive",
			map[string][]string{"content-type": {"text/html"}},
			"Content-Type", "application/json",
			map[string][]string{"content-type": {"application/json"}},
		},
	} {
		t.Run(tt.name, func(t *testing.T) {
			m := NewFakeHeaderMap(tt.initial)
			m.Set(tt.key, tt.value)
			if !reflect.DeepEqual(m.Headers, tt.expected) {
				t.Errorf("got %v, want %v", m.Headers, tt.expected)
			}
		})
	}
}

func TestFakeHeaderMap_Add(t *testing.T) {
	for _, tt := range []struct {
		name     string
		initial  map[string][]string
		key      string
		value    string
		expected map[string][]string
	}{
		{
			"new header",
			map[string][]string{},
			"x-forwarded-for", "10.0.0.1",
			map[string][]string{"x-forwarded-for": {"10.0.0.1"}},
		},
		{
			"appends to existing",
			map[string][]string{"x-forwarded-for": {"10.0.0.1"}},
			"x-forwarded-for", "10.0.0.2",
			map[string][]string{"x-forwarded-for": {"10.0.0.1", "10.0.0.2"}},
		},
		{
			"case insensitive append",
			map[string][]string{"x-forwarded-for": {"10.0.0.1"}},
			"X-Forwarded-For", "10.0.0.2",
			map[string][]string{"x-forwarded-for": {"10.0.0.1", "10.0.0.2"}},
		},
	} {
		t.Run(tt.name, func(t *testing.T) {
			m := NewFakeHeaderMap(tt.initial)
			m.Add(tt.key, tt.value)
			if !reflect.DeepEqual(m.Headers, tt.expected) {
				t.Errorf("got %v, want %v", m.Headers, tt.expected)
			}
		})
	}
}

func TestFakeHeaderMap_Remove(t *testing.T) {
	for _, tt := range []struct {
		name     string
		initial  map[string][]string
		key      string
		expected map[string][]string
	}{
		{
			"existing header",
			map[string][]string{"authorization": {"Bearer token123"}},
			"authorization",
			map[string][]string{},
		},
		{
			"case insensitive remove",
			map[string][]string{"authorization": {"Bearer token123"}},
			"Authorization",
			map[string][]string{},
		},
		{
			"missing header",
			map[string][]string{},
			"authorization",
			map[string][]string{},
		},
	} {
		t.Run(tt.name, func(t *testing.T) {
			m := NewFakeHeaderMap(tt.initial)
			m.Remove(tt.key)
			if !reflect.DeepEqual(m.Headers, tt.expected) {
				t.Errorf("got %v, want %v", m.Headers, tt.expected)
			}
		})
	}
}
