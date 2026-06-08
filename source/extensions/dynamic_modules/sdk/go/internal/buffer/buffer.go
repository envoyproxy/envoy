// Package buffer holds SDK-internal helpers for decoding runtime values into caller-provided byte
// buffers. The runtime writes up to the buffer capacity and reports the full size, so these helpers
// grow the buffer and retry when it was too small. It lives under internal/ so it stays out of the
// public module API.
package buffer

// Fill decodes a single value into buf, growing it and retrying when the value did not fit. The
// fill callback writes up to cap(buf) bytes and returns the full size of the value and whether it
// exists. On success the returned slice holds exactly the bytes written. When the value does not
// exist, buf is returned unchanged with false. The size is stable during a flush, so this
// converges in at most two iterations. Pass buf[:0] and assign the result back to reuse the
// allocation.
func Fill(buf []byte, fill func(buf []byte) (size uint64, ok bool)) ([]byte, bool) {
	for {
		size, ok := fill(buf)
		if !ok {
			return buf, false
		}
		if size <= uint64(cap(buf)) {
			return buf[:size], true
		}
		buf = make([]byte, 0, size)
	}
}

// FillTwo decodes a name and a value produced by a single call, growing whichever did not fit and
// retrying until both fit. The fill callback rewrites both buffers on every call, so a retry
// repopulates the one that already fit before the final reslice. Both sizes are stable during a
// flush, so this converges in at most two iterations. When the value does not exist, the buffers
// are returned unchanged with false. The nameBuf and valueBuf slices must not share a backing
// array.
func FillTwo(
	nameBuf, valueBuf []byte,
	fill func(nameBuf, valueBuf []byte) (nameSize, valueSize uint64, ok bool),
) ([]byte, []byte, bool) {
	for {
		nameSize, valueSize, ok := fill(nameBuf, valueBuf)
		if !ok {
			return nameBuf, valueBuf, false
		}
		nameFits := nameSize <= uint64(cap(nameBuf))
		valueFits := valueSize <= uint64(cap(valueBuf))
		if nameFits && valueFits {
			return nameBuf[:nameSize], valueBuf[:valueSize], true
		}
		if !nameFits {
			nameBuf = make([]byte, 0, nameSize)
		}
		if !valueFits {
			valueBuf = make([]byte, 0, valueSize)
		}
	}
}
