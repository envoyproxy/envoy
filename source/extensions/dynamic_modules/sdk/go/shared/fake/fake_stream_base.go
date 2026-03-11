package fake

import (
	"unsafe"

	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

type FakeHeaderMap struct {
	Headers map[string][]string
}

func NewFakeHeaderMap(headers map[string][]string) *FakeHeaderMap {
	return &FakeHeaderMap{
		Headers: headers,
	}
}

func (m *FakeHeaderMap) Get(key string) []shared.UnsafeEnvoyBuffer {
	values := m.Headers[key]
	result := make([]shared.UnsafeEnvoyBuffer, len(values))
	for i, v := range values {
		result[i] = shared.UnsafeEnvoyBuffer{Ptr: unsafe.StringData(v), Len: uint64(len(v))}
	}
	return result
}

func (m *FakeHeaderMap) GetOne(key string) shared.UnsafeEnvoyBuffer {
	values := m.Headers[key]
	if len(values) > 0 {
		v := values[0]
		return shared.UnsafeEnvoyBuffer{Ptr: unsafe.StringData(v), Len: uint64(len(v))}
	}
	return shared.UnsafeEnvoyBuffer{}
}

func (m *FakeHeaderMap) GetAll() [][2]shared.UnsafeEnvoyBuffer {
	var result [][2]shared.UnsafeEnvoyBuffer
	for k, vs := range m.Headers {
		for _, v := range vs {
			result = append(result, [2]shared.UnsafeEnvoyBuffer{
				{Ptr: unsafe.StringData(k), Len: uint64(len(k))},
				{Ptr: unsafe.StringData(v), Len: uint64(len(v))},
			})
		}
	}
	return result
}

func (m *FakeHeaderMap) Set(key string, value string) {
	m.Headers[key] = []string{value}
}

func (m *FakeHeaderMap) Add(key string, value string) {
	m.Headers[key] = append(m.Headers[key], value)
}

func (m *FakeHeaderMap) Remove(key string) {
	delete(m.Headers, key)
}

type FakeBodyBuffer struct {
	Body []byte
}

func NewFakeBodyBuffer(body []byte) *FakeBodyBuffer {
	return &FakeBodyBuffer{
		Body: body,
	}
}

func (b *FakeBodyBuffer) GetChunks() []shared.UnsafeEnvoyBuffer {
	return []shared.UnsafeEnvoyBuffer{
		{Ptr: unsafe.SliceData(b.Body), Len: uint64(len(b.Body))},
	}
}

func (b *FakeBodyBuffer) GetSize() uint64 {
	return uint64(len(b.Body))
}

func (b *FakeBodyBuffer) Drain(size uint64) {
	if size >= uint64(len(b.Body)) {
		b.Body = []byte{}
	}
	b.Body = b.Body[size:]
}

func (b *FakeBodyBuffer) Append(data []byte) {
	b.Body = append(b.Body, data...)
}
