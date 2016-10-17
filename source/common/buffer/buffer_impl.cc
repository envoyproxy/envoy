#include "buffer_impl.h"

#include "common/common/assert.h"

#include "event2/buffer.h"

namespace Buffer {

void ImplBase::add(const void* data, uint64_t size) { evbuffer_add(&buffer(), data, size); }

void ImplBase::add(const std::string& data) { evbuffer_add(&buffer(), data.c_str(), data.size()); }

void ImplBase::add(const Instance& data) {
  uint64_t num_slices = data.getRawSlices(nullptr, 0);
  RawSlice slices[num_slices];
  data.getRawSlices(slices, num_slices);
  for (RawSlice& slice : slices) {
    add(slice.mem_, slice.len_);
  }
}

void ImplBase::drain(uint64_t size) {
  ASSERT(size <= length());
  int rc = evbuffer_drain(&buffer(), size);
  ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);
}

uint64_t ImplBase::getRawSlices(RawSlice* out, uint64_t out_size) const {
  evbuffer_iovec iovecs[out_size];
  uint64_t needed_size = evbuffer_peek(&buffer(), -1, nullptr, iovecs, out_size);
  for (uint64_t i = 0; i < std::min(out_size, needed_size); i++) {
    out[i].mem_ = iovecs[i].iov_base;
    out[i].len_ = iovecs[i].iov_len;
  }
  return needed_size;
}

uint64_t ImplBase::length() const { return evbuffer_get_length(&buffer()); }

void* ImplBase::linearize(uint32_t size) {
  ASSERT(size <= length());
  return evbuffer_pullup(&buffer(), size);
}

void ImplBase::move(Instance& rhs) {
  // We do the static cast here because in practice we only have one buffer implementation right
  // now and this is safe. Using the evbuffer move routines require having access to both evbuffers.
  // This is a reasonable compromise in a high performance path where we want to maintain an
  // abstraction in case we get rid of evbuffer later.
  int rc = evbuffer_add_buffer(&buffer(), &static_cast<ImplBase&>(rhs).buffer());
  ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);
}

void ImplBase::move(Instance& rhs, uint64_t length) {
  // See move() above for why we do the static cast.
  int rc = evbuffer_remove_buffer(&static_cast<ImplBase&>(rhs).buffer(), &buffer(), length);
  ASSERT(static_cast<uint64_t>(rc) == length);
  UNREFERENCED_PARAMETER(rc);
}

ssize_t ImplBase::search(const void* data, uint64_t size, size_t start) const {
  evbuffer_ptr start_ptr;
  if (-1 == evbuffer_ptr_set(&buffer(), &start_ptr, start, EVBUFFER_PTR_SET)) {
    return -1;
  }

  evbuffer_ptr result_ptr =
      evbuffer_search(&buffer(), static_cast<const char*>(data), size, &start_ptr);
  return result_ptr.pos;
}

OwnedImpl::OwnedImpl() : buffer_(evbuffer_new()) {}

OwnedImpl::OwnedImpl(const std::string& data) : OwnedImpl() { add(data); }

OwnedImpl::OwnedImpl(const Instance& data) : OwnedImpl() { add(data); }

OwnedImpl::OwnedImpl(const void* data, uint64_t size) : OwnedImpl() { add(data, size); }

} // Buffer
