#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <cstddef>

namespace spdy {

// TODO(danzh): Fill out SpdyMemSliceImpl.
//
// SpdyMemSliceImpl wraps a reference counted MemSlice and only provides partial
// interfaces of MemSlice.
class SpdyMemSliceImpl {
public:
  // Constructs an empty SpdyMemSliceImpl that contains an empty MemSlice.
  SpdyMemSliceImpl();

  // Constructs a SpdyMemSlice with reference count 1 to a newly allocated data
  // buffer of |length| bytes.
  explicit SpdyMemSliceImpl(size_t length);

  // Constructs a reference-counted MemSlice to |data|.
  SpdyMemSliceImpl(const char* data, size_t length);

  SpdyMemSliceImpl(const SpdyMemSliceImpl& other) = delete;
  SpdyMemSliceImpl& operator=(const SpdyMemSliceImpl& other) = delete;

  // Move constructors. |other| will not hold a reference to the data buffer
  // after this call completes.
  SpdyMemSliceImpl(SpdyMemSliceImpl&& other) = default;
  SpdyMemSliceImpl& operator=(SpdyMemSliceImpl&& other) = default;

  ~SpdyMemSliceImpl();

  // Returns a char pointer to underlying data buffer.
  const char* data() const;
  // Returns the length of underlying data buffer.
  size_t length() const;
};

} // namespace spdy
