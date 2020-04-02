#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

#include <cstdint>
#include <string>

#include "common/api/os_sys_calls_impl.h"

#define DWORD_MAX UINT32_MAX

namespace Envoy {
namespace Api {
namespace {

using WSABUFPtr = std::unique_ptr<WSABUF[]>;

struct wsabufResult {
  DWORD num_vec_;
  WSABUFPtr wsabuf_;
};

wsabufResult iovecToWSABUF(const iovec* vec, int in_vec) {
  DWORD num_vec = 0;
  for (int i = 0; i < in_vec; i++) {
    size_t cur_len = vec[i].iov_len;
    num_vec++;
    while (cur_len > DWORD_MAX) {
      num_vec++;
      cur_len -= DWORD_MAX;
    }
  }

  WSABUFPtr wsa_buf(new WSABUF[num_vec]);

  WSABUF* wsa_elt = wsa_buf.get();
  for (int i = 0; i < in_vec; i++) {
    CHAR* base = static_cast<CHAR*>(vec[i].iov_base);
    size_t cur_len = vec[i].iov_len;
    do {
      wsa_elt->buf = base;
      if (cur_len > DWORD_MAX) {
        wsa_elt->len = DWORD_MAX;
      } else {
        wsa_elt->len = static_cast<DWORD>(cur_len);
      }
      base += wsa_elt->len;
      cur_len -= wsa_elt->len;
      ++wsa_elt;
    } while (cur_len > 0);
  }

  return {num_vec, std::move(wsa_buf)};
}

LPFN_WSARECVMSG getFnPtrWSARecvMsg() {
  LPFN_WSARECVMSG recvmsg_fn_ptr = NULL;
  GUID recvmsg_guid = WSAID_WSARECVMSG;
  SOCKET sock = INVALID_SOCKET;
  DWORD bytes_received = 0;

  sock = socket(AF_INET6, SOCK_DGRAM, 0);

  RELEASE_ASSERT(
      WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER, &recvmsg_guid, sizeof(recvmsg_guid),
               &recvmsg_fn_ptr, sizeof(recvmsg_fn_ptr), &bytes_received, NULL,
               NULL) != SOCKET_ERROR,
      "WSAIoctl SIO_GET_EXTENSION_FUNCTION_POINTER for WSARecvMsg failed, not implemented?");

  closesocket(sock);

  return recvmsg_fn_ptr;
}

using WSAMSGPtr = std::unique_ptr<WSAMSG>;

WSAMSGPtr msghdrToWSAMSG(const msghdr* msg) {
  WSAMSGPtr wsa_msg(new WSAMSG);

  wsa_msg->name = reinterpret_cast<SOCKADDR*>(msg->msg_name);
  wsa_msg->namelen = msg->msg_namelen;
  wsabufResult wsabuf = iovecToWSABUF(msg->msg_iov, msg->msg_iovlen);
  wsa_msg->lpBuffers = wsabuf.wsabuf_.get();
  wsa_msg->dwBufferCount = wsabuf.num_vec_;
  WSABUF control;
  control.buf = reinterpret_cast<CHAR*>(msg->msg_control);
  control.len = msg->msg_controllen;
  wsa_msg->Control = control;
  wsa_msg->dwFlags = msg->msg_flags;

  return wsa_msg;
}

} // namespace

SysCallIntResult OsSysCallsImpl::bind(os_fd_t sockfd, const sockaddr* addr, socklen_t addrlen) {
  const int rc = ::bind(sockfd, addr, addrlen);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallIntResult OsSysCallsImpl::chmod(const std::string& path, mode_t mode) {
  const int rc = ::_chmod(path.c_str(), mode);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::ioctl(os_fd_t sockfd, unsigned long int request, void* argp) {
  const int rc = ::ioctlsocket(sockfd, request, static_cast<u_long*>(argp));
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallIntResult OsSysCallsImpl::close(os_fd_t fd) {
  const int rc = ::closesocket(fd);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallSizeResult OsSysCallsImpl::writev(os_fd_t fd, const iovec* iov, int num_iov) {
  DWORD bytes_sent;
  wsabufResult wsabuf = iovecToWSABUF(iov, num_iov);

  const int rc =
      ::WSASend(fd, wsabuf.wsabuf_.get(), wsabuf.num_vec_, &bytes_sent, 0, nullptr, nullptr);
  if (SOCKET_FAILURE(rc)) {
    return {-1, ::WSAGetLastError()};
  }
  return {bytes_sent, 0};
}

SysCallSizeResult OsSysCallsImpl::readv(os_fd_t fd, const iovec* iov, int num_iov) {
  DWORD bytes_received;
  DWORD flags = 0;
  wsabufResult wsabuf = iovecToWSABUF(iov, num_iov);

  const int rc = ::WSARecv(fd, wsabuf.wsabuf_.get(), wsabuf.num_vec_, &bytes_received, &flags,
                           nullptr, nullptr);
  if (SOCKET_FAILURE(rc)) {
    return {-1, ::WSAGetLastError()};
  }
  return {bytes_received, 0};
}

SysCallSizeResult OsSysCallsImpl::recv(os_fd_t socket, void* buffer, size_t length, int flags) {
  const ssize_t rc = ::recv(socket, static_cast<char*>(buffer), length, flags);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallSizeResult OsSysCallsImpl::recvmsg(os_fd_t sockfd, msghdr* msg, int flags) {
  DWORD bytes_received;
  LPFN_WSARECVMSG recvmsg_fn_ptr = getFnPtrWSARecvMsg();
  WSAMSGPtr wsa_msg = msghdrToWSAMSG(msg);
  // Windows supports only a single flag on input to WSARecvMsg
  wsa_msg->dwFlags = flags & MSG_PEEK;
  const int rc = recvmsg_fn_ptr(sockfd, wsa_msg.get(), &bytes_received, nullptr, nullptr);
  if (rc == SOCKET_ERROR) {
    return {-1, ::WSAGetLastError()};
  }
  msg->msg_namelen = wsa_msg->namelen;
  msg->msg_flags = wsa_msg->dwFlags;
  msg->msg_controllen = wsa_msg->Control.len;
  return {bytes_received, 0};
}

SysCallIntResult OsSysCallsImpl::recvmmsg(os_fd_t sockfd, struct mmsghdr* msgvec, unsigned int vlen,
                                          int flags, struct timespec* timeout) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

bool OsSysCallsImpl::supportsMmsg() const {
  // Windows doesn't support it.
  return false;
}

SysCallIntResult OsSysCallsImpl::ftruncate(int fd, off_t length) {
  const int rc = ::_chsize_s(fd, length);
  return {rc, rc == 0 ? 0 : errno};
}

SysCallPtrResult OsSysCallsImpl::mmap(void* addr, size_t length, int prot, int flags, int fd,
                                      off_t offset) {
  PANIC("mmap not implemented on Windows");
}

SysCallIntResult OsSysCallsImpl::stat(const char* pathname, struct stat* buf) {
  const int rc = ::stat(pathname, buf);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::setsockopt(os_fd_t sockfd, int level, int optname,
                                            const void* optval, socklen_t optlen) {
  const int rc = ::setsockopt(sockfd, level, optname, static_cast<const char*>(optval), optlen);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallIntResult OsSysCallsImpl::getsockopt(os_fd_t sockfd, int level, int optname, void* optval,
                                            socklen_t* optlen) {
  const int rc = ::getsockopt(sockfd, level, optname, static_cast<char*>(optval), optlen);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallSocketResult OsSysCallsImpl::socket(int domain, int type, int protocol) {
  const os_fd_t rc = ::socket(domain, type, protocol);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallSizeResult OsSysCallsImpl::sendmsg(os_fd_t sockfd, const msghdr* msg, int flags) {
  DWORD bytes_received;
  // if overlapped and/or completion routines are supported adjust the arguments accordingly
  const int rc =
      ::WSASendMsg(sockfd, msghdrToWSAMSG(msg).get(), flags, &bytes_received, nullptr, nullptr);
  if (rc == SOCKET_ERROR) {
    return {-1, ::WSAGetLastError()};
  }
  return {bytes_received, 0};
}

SysCallIntResult OsSysCallsImpl::getsockname(os_fd_t sockfd, sockaddr* addr, socklen_t* addrlen) {
  const int rc = ::getsockname(sockfd, addr, addrlen);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallIntResult OsSysCallsImpl::gethostname(char* name, size_t length) {
  const int rc = ::gethostname(name, length);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallIntResult OsSysCallsImpl::getpeername(os_fd_t sockfd, sockaddr* name, socklen_t* namelen) {
  const int rc = ::getpeername(sockfd, name, namelen);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallIntResult OsSysCallsImpl::setsocketblocking(os_fd_t sockfd, bool blocking) {
  u_long io_mode = blocking ? 0 : 1;
  const int rc = ::ioctlsocket(sockfd, FIONBIO, &io_mode);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallIntResult OsSysCallsImpl::connect(os_fd_t sockfd, const sockaddr* addr, socklen_t addrlen) {
  const int rc = ::connect(sockfd, addr, addrlen);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallIntResult OsSysCallsImpl::shutdown(os_fd_t sockfd, int how) {
  const int rc = ::shutdown(sockfd, how);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallIntResult OsSysCallsImpl::socketpair(int domain, int type, int protocol, os_fd_t sv[2]) {
  if (sv == nullptr) {
    return {SOCKET_ERROR, WSAEINVAL};
  }

  sv[0] = sv[1] = INVALID_SOCKET;

  SysCallSocketResult socket_result = socket(domain, type, protocol);
  if (SOCKET_INVALID(socket_result.rc_)) {
    return {SOCKET_ERROR, socket_result.errno_};
  }

  os_fd_t listener = socket_result.rc_;

  typedef union {
    struct sockaddr_storage sa;
    struct sockaddr_in in;
    struct sockaddr_in6 in6;
  } sa_union;
  sa_union a = {};
  socklen_t sa_size = sizeof(a);

  a.sa.ss_family = domain;
  if (domain == AF_INET) {
    a.in.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    a.in.sin_port = 0;
  } else if (domain == AF_INET6) {
    a.in6.sin6_addr = in6addr_loopback;
    a.in6.sin6_port = 0;
  } else {
    return {SOCKET_ERROR, WSAEINVAL};
  }

  auto onErr = [this, listener, sv]() -> void {
    ::closesocket(listener);
    ::closesocket(sv[0]);
    ::closesocket(sv[1]);
    sv[0] = INVALID_SOCKET;
    sv[1] = INVALID_SOCKET;
  };

  SysCallIntResult int_result = bind(listener, reinterpret_cast<sockaddr*>(&a), sa_size);
  if (int_result.rc_ == SOCKET_ERROR) {
    onErr();
    return int_result;
  }

  int_result = listen(listener, 1);
  if (int_result.rc_ == SOCKET_ERROR) {
    onErr();
    return int_result;
  }

  socket_result = socket(domain, type, protocol);
  if (SOCKET_INVALID(socket_result.rc_)) {
    onErr();
    return {SOCKET_ERROR, socket_result.errno_};
  }
  sv[0] = socket_result.rc_;

  a = {};
  int_result = getsockname(listener, reinterpret_cast<sockaddr*>(&a), &sa_size);
  if (int_result.rc_ == SOCKET_ERROR) {
    onErr();
    return int_result;
  }

  int_result = connect(sv[0], reinterpret_cast<sockaddr*>(&a), sa_size);
  if (int_result.rc_ == SOCKET_ERROR) {
    onErr();
    return int_result;
  }

  socket_result.rc_ = ::accept(listener, nullptr, nullptr);
  if (SOCKET_INVALID(socket_result.rc_)) {
    socket_result.errno_ = ::WSAGetLastError();
    onErr();
    return {SOCKET_ERROR, socket_result.errno_};
  }
  sv[1] = socket_result.rc_;

  ::closesocket(listener);
  return {0, 0};
}

SysCallIntResult OsSysCallsImpl::listen(os_fd_t sockfd, int backlog) {
  const int rc = ::listen(sockfd, backlog);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallSizeResult OsSysCallsImpl::write(os_fd_t sockfd, const void* buffer, size_t length) {
  const ssize_t rc = ::send(sockfd, static_cast<const char*>(buffer), length, 0);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

} // namespace Api
} // namespace Envoy
