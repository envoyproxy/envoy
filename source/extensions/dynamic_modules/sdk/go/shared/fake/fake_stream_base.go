package fake

type FakeHeaderMap struct {
	Headers map[string][]string
}

func NewFakeHeaderMap(headers map[string][]string) *FakeHeaderMap {
	return &FakeHeaderMap{
		Headers: headers,
	}
}

func (m *FakeHeaderMap) Get(key string) []string {
	return m.Headers[key]
}

func (m *FakeHeaderMap) GetOne(key string) string {
	values := m.Headers[key]
	if len(values) > 0 {
		return values[0]
	}
	return ""
}

func (m *FakeHeaderMap) GetAll() [][2]string {
	var result [][2]string
	for k, vs := range m.Headers {
		for _, v := range vs {
			result = append(result, [2]string{k, v})
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

func (b *FakeBodyBuffer) GetChunks() [][]byte {
	return [][]byte{b.Body}
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
