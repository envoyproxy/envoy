#pragma once

#include "envoy/common/pure.h"

#include "source/common/network/address_impl.h"

namespace Envoy {
namespace Io {

using CompletionCb = std::function<void(void*, int32_t)>;

enum class IoUringResult { Ok, Busy, Failed };

class IoUring {
public:
  virtual ~IoUring() = default;

  virtual os_fd_t registerEventfd() PURE;
  virtual void unregisterEventfd() PURE;
  virtual bool isEventfdRegistered() const PURE;
  virtual void forEveryCompletion(CompletionCb completion_cb) PURE;
  virtual IoUringResult prepareAccept(os_fd_t fd, struct sockaddr* remote_addr,
                                      socklen_t* remote_addr_len, void* user_data) PURE;
  virtual IoUringResult prepareConnect(os_fd_t fd,
                                       const Network::Address::InstanceConstSharedPtr& address,
                                       void* user_data) PURE;
  virtual IoUringResult prepareReadv(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                                     off_t offset, void* user_data) PURE;
  virtual IoUringResult prepareWritev(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                                      off_t offset, void* user_data) PURE;
  virtual IoUringResult prepareClose(os_fd_t fd, void* user_data) PURE;
  virtual IoUringResult submit() PURE;
};

class IoUringFactory {
public:
  virtual ~IoUringFactory() = default;

  virtual IoUring& getOrCreateUring() const PURE;
};

} // namespace Io
} // namespace Envoy
