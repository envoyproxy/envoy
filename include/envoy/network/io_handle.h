#pragma once

#include "envoy/api/os_sys_calls.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Network {

class IoHandle {
public:
  virtual ~IoHandle() {}

  virtual Api::SysCallSizeResult readv(const iovec* iovec, int num_iovec) PURE;

  virtual Api::SysCallSizeResult writev(const iovec* iovec, int num_iovec) PURE;

  virtual Api::SysCallIntResult close() PURE;

  virtual bool isClosed() PURE;

  virtual Api::SysCallIntResult bind(const sockaddr* addr, socklen_t addrlen) PURE;

  virtual Api::SysCallIntResult connect(const struct sockaddr* serv_addr, socklen_t addrlen) PURE;

  virtual Api::SysCallIntResult setSocketOption(int level, int optname, const void* optval,
                                           socklen_t optlen) PURE;

  virtual Api::SysCallIntResult getSocketOption(int level, int optname, void* optval,
                                           socklen_t* optlen) PURE;

  virtual Api::SysCallIntResult getSocketName(struct sockaddr* addr, socklen_t* addr_len) PURE;

  virtual Api::SysCallIntResult getPeerName(struct sockaddr* addr, socklen_t* addr_len) PURE;

  virtual Api::SysCallIntResult setSocketFlag(int flag) PURE;

  virtual Api::SysCallIntResult getSocketFlag() PURE;

  virtual Api::SysCallIntResult listen(int backlog) PURE;

  virtual std::unique_ptr<IoHandle> dup() PURE;

  virtual Api::SysCallIntResult shutdown(int how) PURE;

  virtual int id() PURE;
};

typedef std::unique_ptr<IoHandle> IoHandlePtr;

} // namespace Network
} // namespace Envoy
