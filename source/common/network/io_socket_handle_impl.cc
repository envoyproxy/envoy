#include "common/network/io_socket_handle_impl.h"

#include <errno.h>

#include <iostream>

#include "common/common/assert.h"

using Envoy::Api::SysCallIntResult;
using Envoy::Api::SysCallSizeResult;

namespace Envoy {
namespace Network {

IoSocketHandleImpl::~IoSocketHandleImpl() {
  if (fd_ != -1) {
    close();
  }
}

SysCallIntResult IoSocketHandleImpl::close() {
  ASSERT(fd_ != -1);
  const int rc = ::close(fd_);
  fd_ = -1;
  return {rc, errno};
}

bool IoSocketHandleImpl::isClosed() const { return fd_ == -1; }

} // namespace Network
} // namespace Envoy
