#include "buffer_impl.h"

#include "common/common/assert.h"

#include "event2/buffer.h"

namespace Buffer {

void ImplBase::add(const void* data, uint64_t size) { evbuffer_add(&buffer(), data, size); }

void ImplBase::add(const std::string& data) { evbuffer_add(&buffer(), data.c_str(), data.size()); }

void ImplBase::add(const Instance& data) {
  std::vector<RawSlice> slices = data.getRawSlices();
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

std::vector<RawSlice> ImplBase::getRawSlices() const {
  int num_needed = evbuffer_peek(&buffer(), -1, nullptr, nullptr, 0);

  std::vector<RawSlice> slices;
  evbuffer_iovec iovecs[num_needed];
  evbuffer_peek(&buffer(), -1, nullptr, iovecs, num_needed);
  for (int i = 0; i < num_needed; i++) {
    slices.emplace_back(RawSlice{iovecs[i].iov_base, iovecs[i].iov_len});
  }

  return slices;
}

uint64_t ImplBase::length() const { return evbuffer_get_length(&buffer()); }

void* ImplBase::linearize(uint32_t size) {
  ASSERT(size <= length());
  return evbuffer_pullup(&buffer(), size);
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
