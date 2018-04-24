#include "common/vpp/vpp_buffer.h"

#include "common/common/assert.h"
#include "common/buffer/buffer_impl.h"
#include "common/vpp/vpp_connection_impl.h"


using Envoy::Network::PostIoAction;

namespace Envoy {
namespace Vpp {

void VppBufferImpl::ioEventCallback(vppcom_ioevent_t*, void *arg) {
  auto * buffer = static_cast<VppBufferImpl*>(arg);
  if (buffer && buffer->ev_ && !buffer->eventActive_) {
    buffer->eventActive_ = true;
    event_active(buffer->ev_, EV_READ, 0);
  }
}

void VppBufferImpl::vppIoEventCallback(evutil_socket_t, short flags, void* arg) {
  Network::VppConnectionImpl* vci = static_cast<Network::VppConnectionImpl *>(arg);
  uint32_t events = (((flags & EV_READ) ? Event::FileReadyType::Read : 0) |
                     ((flags & EV_WRITE) ? Event::FileReadyType::Write: 0));
  vci->onIoEvent(events);
}

void VppBufferImpl::registerVppRxIoEventCallback(
        Event::DispatcherImpl &dispatcher, void *arg) {
      ev_ = event_new(&dispatcher.base(), -1, EV_READ | EV_PERSIST,
                      vppIoEventCallback, arg);
      event_add(ev_, NULL);
      vppcom_session_register_ioevent_cb(sessionId_, ioEventCallback, 1, this);
}

// Buffer::RawSlice is the same structure as evbuffer_iovec. This was put into place to avoid leaking
// libevent into most code since we will likely replace evbuffer with our own implementation at
// some point. However, we can avoid a bunch of copies since the structure is the same.
static_assert(sizeof(Buffer::RawSlice) == sizeof(evbuffer_iovec), "Buffer::RawSlice != evbuffer_iovec");
static_assert(offsetof(Buffer::RawSlice, mem_) == offsetof(evbuffer_iovec, iov_base),
              "Buffer::RawSlice != evbuffer_iovec");
static_assert(offsetof(Buffer::RawSlice, len_) == offsetof(evbuffer_iovec, iov_len),
              "Buffer::RawSlice != evbuffer_iovec");

void VppBufferImpl::add(const void* data, uint64_t size) {
  vppBuffer_.insert(vppBuffer_.end(), const_cast<char*>(reinterpret_cast<const char*>(data)),
                    const_cast<char*>(reinterpret_cast<const char*>(data)) + size);
}


void VppBufferImpl::add(const std::string& data) {
  add(data.c_str(), data.size());
}

void VppBufferImpl::addBufferFragment(Buffer::BufferFragment& fragment) {
  /*
   * alagalah: This function SHOULD be only appending a reference to the parm: fragment,
   * not memcpying it. The original code hands libevent a callback to handle cleanup.
   */
  add(fragment.data(), fragment.size());
}

void VppBufferImpl::add(const Buffer::Instance& data) {
  uint64_t num_slices = 1;
  Buffer::RawSlice slice;
  data.getRawSlices(&slice, num_slices);
  add(slice.mem_, slice.len_);

}

void VppBufferImpl::commit(Buffer::RawSlice* /*iovecs*/, uint64_t /*num_iovecs*/) {
  /* alagalah
   * Please read descriptions of evbuffer_reserve_space and evbuffer_commit_space.
   * Think of this as pre-allocating and getting access to a part of a vector.
   * ie.
   * You vec_validate(foo, 1000);
   * You say "I want from 100-200 back as a pointer"
   * You then write into the pointer you got back...
   * Its not ACTUALLY written into the evbuffer until you call evbuffer_commit_space, at
   * which time if you only used 100-150, then 151-200 would be "freed" and a shuffle occurs.
   *
   * In reality its not REALLY like this, its more a buffer fragment allocation scheme by reference,
   * but the analogy holds up.
   *
   * Since this is the commit, its just going to return 0, and "reserve" will return a pointer for now.
   */
  int rc = 0;
      //evbuffer_commit_space(buffer_.get(), reinterpret_cast<evbuffer_iovec*>(iovecs), num_iovecs);
  ASSERT(0);
  UNREFERENCED_PARAMETER(rc);
}

void VppBufferImpl::copyOut(size_t start, uint64_t size, void* data) const {
  ASSERT(start + size <= length());
  (void) data;
  int rc = 0;//evbuffer_ptr_set(buffer_.get(), &start_ptr, start, EVBUFFER_PTR_SET);
  ASSERT(rc != -1);
  UNREFERENCED_PARAMETER(rc);

  ev_ssize_t copied = 42; //evbuffer_copyout_from(buffer_.get(), &start_ptr, data, size);
  ASSERT(static_cast<uint64_t>(copied) == size);
  UNREFERENCED_PARAMETER(copied);
}

void VppBufferImpl::drain(uint64_t size) {
  auto begin_it = vppBuffer_.begin();
  auto end_it = vppBuffer_.end();

  if (size < vppBuffer_.size())
    end_it = vppBuffer_.begin()+size;

  (void) vppBuffer_.erase(begin_it, end_it);
}

uint64_t VppBufferImpl::getRawSlices(Buffer::RawSlice* out, uint64_t) const {
  out->mem_ = const_cast<unsigned char*>(vppBuffer_.data()); // DAW_DEBUG: ephemeral pointer SNAFU???
  out->len_ = vppBuffer_.size();
  /*
   * Return the number of slices, see OwnedImpl that is based on libevent.
   * This approach is kind of gross but its in the Interface, and the alternative
   * kind of ends up in a recursive constructor loop.
   */
  return 1; // The Number of "slices" in this case, we have one.
}

uint64_t VppBufferImpl::length() const {
  return vppBuffer_.size();
}

void* VppBufferImpl::linearize(uint32_t size) {
  ASSERT(size <= length());
  return nullptr; //evbuffer_pullup(buffer_.get(), size);
}

void VppBufferImpl::move(Instance& rhs ) {
  add(rhs);
}

void VppBufferImpl::move(Instance& rhs, uint64_t length) {
  UNREFERENCED_PARAMETER(length);
  add(rhs);
}

int VppBufferImpl::read(int fd, uint64_t max_length) {
  int rv = vppcom_session_attr(fd, VPPCOM_ATTR_GET_NREAD, nullptr, nullptr);

  if (!max_length)  {
    if (!rv) {
      eventActive_ = false;
      rv = 1;
    } else {
      rv = 0;
    }
  } else if (rv) {
      size_t csize = vppBuffer_.size();
      vppBuffer_.resize(csize + max_length);
      buffer_ = &vppBuffer_.data()[csize];
      rv = vppcom_session_read(fd, buffer_, max_length);

      if (rv < 0) {
          errno = -rv;
          if (rv == VPPCOM_EAGAIN) {
              eventActive_ = false;
          }
          rv = ((rv == VPPCOM_EBADFD) || (rv == VPPCOM_ECONNRESET)) ? 0 : -1;
      } else {
          vppBuffer_.resize(csize + rv);
      }
  } else {
      eventActive_ = false;
      errno = EAGAIN;
      rv = -1;
  }
  return rv;
}

void VppBufferImpl::setWatermarks(uint32_t low_watermark, uint32_t high_watermark) {
  ASSERT(low_watermark < high_watermark || (high_watermark == 0 && low_watermark == 0));
  low_watermark_ = low_watermark;
  high_watermark_ = high_watermark;
  checkHighWatermark();
  checkLowWatermark();
}

void VppBufferImpl::checkLowWatermark() {
  if (!above_high_watermark_called_ ||
      (high_watermark_ != 0 && VppBufferImpl::length() >= low_watermark_)) {
    return;
  }

  above_high_watermark_called_ = false;
  below_low_watermark_();
}

void VppBufferImpl::checkHighWatermark() {
  if (above_high_watermark_called_ || high_watermark_ == 0 ||
      VppBufferImpl::length() <= high_watermark_) {
    return;
  }
  above_high_watermark_called_ = true;
  above_high_watermark_();
}

uint64_t VppBufferImpl::reserve(uint64_t length, Buffer::RawSlice* iovecs, uint64_t num_iovecs) {
  (void)length;
  (void)iovecs;
  (void)num_iovecs;
  uint64_t ret = 0; //evbuffer_reserve_space(buffer_.get(), length, reinterpret_cast<evbuffer_iovec*>(iovecs), num_iovecs);
  ASSERT(ret >= 1);
  return ret;
}

ssize_t VppBufferImpl::search(const void* data, uint64_t size, size_t start) const {
  (void) data;
  (void) size;
  (void) start;
  return ~0;
}

int VppBufferImpl::write(int fd) {
  int rc = vppcom_session_write(fd, vppBuffer_.data(), vppBuffer_.size());
  if (rc < 0) {
    errno = -rc;
    rc = -1;
  } else {
    eventActive_ = false;
  }
  return rc;
}

VppBufferImpl::VppBufferImpl() {
  vppBuffer_ = std::vector<unsigned char>();
  eventActive_ = false;
}

VppBufferImpl::VppBufferImpl(const std::string& data) : VppBufferImpl() { add(data); }

VppBufferImpl::VppBufferImpl(const Instance& data) : VppBufferImpl() { add(data); }

VppBufferImpl::VppBufferImpl(const void* data, uint64_t size) : VppBufferImpl() { add(data, size); }

} // namespace Vpp
} // namespace Envoy
