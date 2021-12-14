#pragma once

#include "envoy/common/pure.h"

#include "source/common/network/address_impl.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace IoUring {

using CompletionCb = std::function<void(void*, int32_t)>;

class IoUring {
public:
  virtual ~IoUring() = default;

  virtual os_fd_t registerEventfd() PURE;
  virtual void unregisterEventfd() PURE;
  virtual bool isEventfdRegistered() const PURE;
  virtual void forEveryCompletion(CompletionCb completion_cb) PURE;
  virtual void prepareAccept(os_fd_t fd, struct sockaddr* remote_addr, socklen_t* remote_addr_len,
                             void* user_data) PURE;
  virtual void prepareConnect(os_fd_t fd, const Network::Address::InstanceConstSharedPtr& address,
                              void* user_data) PURE;
  virtual void prepareReadv(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs, off_t offset,
                            void* user_data) PURE;
  virtual void prepareWritev(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs, off_t offset,
                             void* user_data) PURE;
  virtual void prepareClose(os_fd_t fd, void* user_data) PURE;
  virtual void submit() PURE;
};

class IoUringFactory {
public:
  virtual ~IoUringFactory() = default;

  virtual IoUring& getOrCreateUring() const PURE;
};

} // namespace IoUring
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
