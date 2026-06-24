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

class IoUringBufferPoolImpl;

class IoUringImpl : public IoUring,
                    public ThreadLocal::ThreadLocalObject,
                    protected Logger::Loggable<Logger::Id::io> {
public:
  IoUringImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
              bool enable_multishot_receive, uint32_t multishot_buffer_size);
  ~IoUringImpl() override;

  os_fd_t registerEventfd() override;
  void unregisterEventfd() override;
  bool isEventfdRegistered() const override;
  void forEveryCompletion(const CompletionCb& completion_cb) override;
  bool hasReadyCompletions() const override;
  IoUringResult prepareAccept(os_fd_t fd, struct sockaddr* remote_addr, socklen_t* remote_addr_len,
                              Request* user_data) override;
  IoUringResult prepareConnect(os_fd_t fd, const Network::Address::InstanceConstSharedPtr& address,
                               Request* user_data) override;
  IoUringResult prepareReadv(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs, off_t offset,
                             Request* user_data) override;
  bool isMultishotEnabled() const override;
  IoUringBufferPoolSharedPtr bufferPool() override;
  IoUringResult prepareReadMultishot(os_fd_t fd, Request* user_data) override;
  IoUringResult prepareWritev(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                              off_t offset, Request* user_data) override;
  IoUringResult prepareClose(os_fd_t fd, Request* user_data) override;
  IoUringResult prepareCancel(Request* cancelling_user_data, Request* user_data) override;
  IoUringResult prepareShutdown(os_fd_t fd, int how, Request* user_data) override;
  IoUringResult submit() override;
  void injectCompletion(os_fd_t fd, Request* user_data, int32_t result) override;
  void removeInjectedCompletion(os_fd_t fd) override;

private:
  // Logs a warning when the completion queue has overflowed. The kernel parks the extra completions
  // in a backlog and flushes them on the next submission, so they are reaped on a later pass.
  void checkCqOverflow();

  struct io_uring ring_ {};
  std::vector<struct io_uring_cqe*> cqes_;
  os_fd_t event_fd_{INVALID_SOCKET};
  std::list<InjectedCompletion> injected_completions_;
  uint64_t cq_overflow_count_{0};
  // The provided buffer pool backing `multishot` reads. Null when `multishot` reads are disabled or
  // not supported by the kernel. Held as a shared_ptr so read fragments can keep the buffer memory
  // alive after this ring is gone.
  std::shared_ptr<IoUringBufferPoolImpl> buffer_pool_;
};

} // namespace Io
} // namespace Envoy
