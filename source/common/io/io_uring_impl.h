#pragma once

#include "envoy/common/io/io_uring.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"

#include "liburing.h"

namespace Envoy {
namespace Io {

bool isIoUringSupported();

struct InjectedCompletion {
  InjectedCompletion(os_fd_t fd, Request* user_data, int32_t result)
      : fd_(fd), user_data_(user_data), result_(result) {}

  const os_fd_t fd_;
  Request* user_data_;
  const int32_t result_;
};

class IoUringImpl : public IoUring,
                    public ThreadLocal::ThreadLocalObject,
                    protected Logger::Loggable<Logger::Id::io> {
public:
  IoUringImpl(uint32_t io_uring_size, bool use_submission_queue_polling);
  ~IoUringImpl() override;

  os_fd_t registerEventfd() override;
  void unregisterEventfd() override;
  bool isEventfdRegistered() const override;
  void forEveryCompletion(const CompletionCb& completion_cb) override;
  IoUringResult prepareAccept(os_fd_t fd, struct sockaddr* remote_addr, socklen_t* remote_addr_len,
                              Request* user_data) override;
  IoUringResult prepareConnect(os_fd_t fd, const Network::Address::InstanceConstSharedPtr& address,
                               Request* user_data) override;
  IoUringResult prepareReadv(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs, off_t offset,
                             Request* user_data) override;
  IoUringResult prepareWritev(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                              off_t offset, Request* user_data) override;
  IoUringResult prepareClose(os_fd_t fd, Request* user_data) override;
  IoUringResult prepareCancel(Request* cancelling_user_data, Request* user_data) override;
  IoUringResult prepareShutdown(os_fd_t fd, int how, Request* user_data) override;
  IoUringResult setupBufRing(uint16_t group_id, uint32_t count, uint32_t buf_size) override;
  IoUringResult prepareRecvMultishot(os_fd_t fd, uint16_t group_id, Request* user_data) override;
  uint8_t* getBufferForBid(uint16_t group_id, uint16_t bid) override;
  void recycleBuffer(uint16_t group_id, uint16_t bid) override;
  IoUringResult submit() override;
  void injectCompletion(os_fd_t fd, Request* user_data, int32_t result) override;
  void removeInjectedCompletion(os_fd_t fd) override;

private:
  struct io_uring ring_ {};
  std::vector<struct io_uring_cqe*> cqes_;
  os_fd_t event_fd_{INVALID_SOCKET};
  std::list<InjectedCompletion> injected_completions_;

  // Buf-ring state. ``buf_ring_`` is set when ``setupBufRing`` succeeds; only one buf-ring is
  // supported per ring for now.
  struct io_uring_buf_ring* buf_ring_{nullptr};
  uint16_t buf_group_id_{0};
  uint32_t buf_count_{0};
  uint32_t buf_size_{0};
  // Backing storage for the buf-ring. ``buf_count_ * buf_size_`` bytes carved into ``buf_count_``
  // contiguous slots; slot ``i`` lives at ``buf_storage_.get() + i * buf_size_``.
  std::unique_ptr<uint8_t[]> buf_storage_;
};

} // namespace Io
} // namespace Envoy
