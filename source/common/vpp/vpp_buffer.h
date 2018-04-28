#pragma once

#include <cstdint>
#include <string>

#include "envoy/network/transport_socket.h"
#include "envoy/buffer/buffer.h"

#include "common/common/non_copyable.h"
#include "common/common/logger.h"
#include "common/event/dispatcher_impl.h"
#include <vcl/vppcom.h>
/*
 * alagalah - goal is to remove this include
 */
#include "event2/buffer.h"
#include "common/event/libevent.h"
#include "common/common/c_smart_ptr.h"
#include "event2/event.h"


namespace Envoy {
namespace Vpp {

enum class InitialState { Client, Server };

class VppBufferFragmentImpl : NonCopyable, public Buffer::BufferFragment {

};

class VppBufferImpl : public Buffer::Instance {
 public:
  VppBufferImpl();
  VppBufferImpl(const std::string& data);
  VppBufferImpl(const Instance& data);
  VppBufferImpl(const void* data, uint64_t size);
  VppBufferImpl(std::function<void()> below_low_watermark,
  std::function<void()> above_high_watermark)
  : below_low_watermark_(below_low_watermark), above_high_watermark_(above_high_watermark) {}

  void add(const void* data, uint64_t size) override;
  void addBufferFragment(Buffer::BufferFragment& fragment) override;
  void add(const std::string& data) override;
  void add(const Instance& data) override;
  void commit(Buffer::RawSlice* iovecs, uint64_t num_iovecs) override;
  void copyOut(size_t start, uint64_t size, void* data) const override;
  void drain(uint64_t size) override;
  uint64_t getRawSlices(Buffer::RawSlice* out, uint64_t out_size) const override;
  uint64_t length() const override;
  void* linearize(uint32_t size) override;
  void move(Instance& rhs) override;
  void move(Instance& rhs, uint64_t length) override;
  int read(int fd, uint64_t max_length) override;
  uint64_t reserve(uint64_t length, Buffer::RawSlice* iovecs, uint64_t num_iovecs) override;
  ssize_t search(const void* data, uint64_t size, size_t start) const override;
  int write(int fd) override;

  void postProcess()  {}
  void setIsRead(bool isRead) { isRead_ = isRead;}
  bool isRead() { return isRead_;}
  std::vector<unsigned char>::pointer buffer()  { return buffer_; }

  void setSessionId(uint32_t sessionId) { sessionId_ = sessionId; }
  uint32_t getSessionId() { return sessionId_;}
  void setWatermarks(uint32_t watermark) { setWatermarks(watermark / 2, watermark); }
  void setWatermarks(uint32_t low_watermark, uint32_t high_watermark);
  uint32_t highWatermark() const { return high_watermark_; }
  void registerVppRxIoEventCallback(Event::DispatcherImpl &dispatcher,
                                    void *arg);
private:
  uint32_t sessionId_;
  bool isRead_;
  bool eventActive_;
  std::vector<unsigned char> vppBuffer_;
  std::vector<unsigned char>::pointer buffer_;
  struct event *ev_;

  static void vppIoEventCallback(evutil_socket_t fd, short flags, void* arg);
  void checkHighWatermark();
  void checkLowWatermark();

  std::function<void()> below_low_watermark_;
  std::function<void()> above_high_watermark_;

  // Used for enforcing buffer limits (off by default). If these are set to non-zero by a call to
  // setWatermarks() the watermark callbacks will be called as described above.
  uint32_t high_watermark_{0};
  uint32_t low_watermark_{0};
  // Tracks the latest state of watermark callbacks.
  // True between the time above_high_watermark_ has been called until above_high_watermark_ has
  // been called.
  bool above_high_watermark_called_{false};
  static void ioEventCallback(vppcom_ioevent_t* ioevent, void* arg);

};

typedef std::unique_ptr<VppBufferImpl> VppBufferImplPtr;

class VppBufferFactory : public Buffer::WatermarkFactory {
 public:
  // Buffer::WatermarkFactory
  Buffer::InstancePtr create(std::function<void()> below_low_watermark,
                     std::function<void()> above_high_watermark) override {
    return Buffer::InstancePtr{new VppBufferImpl(below_low_watermark, above_high_watermark)};
  }
};

} // namespace Vpp
} // namespace Envoy
