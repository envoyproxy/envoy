#include "envoy/network/io_handle.h"

#include "common/api/os_sys_calls_impl.h"

namespace Envoy {
namespace Network {

class FdIoHandleImpl : public virtual IoHandle {
public:
  explicit FdIoHandleImpl(int fd);

  ~FdIoHandleImpl() override{};

  Api::SysCallSizeResult readv(const iovec* iovec, int num_iovec) override;

  Api::SysCallSizeResult writev(const iovec* iovec, int num_iovec) override;

  Api::SysCallIntResult close() override;

  bool isClosed() override;

  Api::SysCallIntResult bind(const sockaddr* addr, socklen_t addrlen) override;

  Api::SysCallIntResult connect(const struct sockaddr* serv_addr, socklen_t addrlen) override;

  Api::SysCallIntResult setSocketOption(int level, int optname, const void* optval,
                                        socklen_t optlen) override;

  Api::SysCallIntResult getSocketOption(int level, int optname, void* optval,
                                        socklen_t* optlen) override;

  Api::SysCallIntResult getSocketName(struct sockaddr* addr, socklen_t* addr_len) override;

  Api::SysCallIntResult getPeerName(struct sockaddr* addr, socklen_t* addr_len) override;

  static IoHandlePtr socket(int domain, int type, int protocol);

  Api::SysCallIntResult setSocketFlag(int flag) override;

  Api::SysCallIntResult getSocketFlag() override;

  Api::SysCallIntResult listen(int backlog) override;

  Api::SysCallIntResult shutdown(int how) override;

  IoHandlePtr dup() override;

  int id() override;

private:
  int fd_;
};

} // namespace Network
} // namespace Envoy
