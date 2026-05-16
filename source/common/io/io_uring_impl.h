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
  // Cap on the number of injected completions processed per event-loop tick.
  // Without this cap, a steady stream of injected completions whose callbacks inject more
  // completions can stall the dispatcher thread indefinitely. When the cap is reached, the
  // remaining injected completions stay queued and the eventfd is re-armed so processing
  // resumes on the next tick.
  static constexpr uint32_t DefaultMaxInjectedCompletionsPerEvent = 1024;

  IoUringImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
              uint32_t max_injected_completions_per_event = DefaultMaxInjectedCompletionsPerEvent);
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
  IoUringResult submit() override;
  void injectCompletion(os_fd_t fd, Request* user_data, int32_t result) override;
  void removeInjectedCompletion(os_fd_t fd) override;

private:
  struct io_uring ring_{};
  std::vector<struct io_uring_cqe*> cqes_;
  os_fd_t event_fd_{INVALID_SOCKET};
  std::list<InjectedCompletion> injected_completions_;
  const uint32_t max_injected_completions_per_event_;
};

} // namespace Io
} // namespace Envoy
