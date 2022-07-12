#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

#include <cstdint>
#include <string>

#include "source/common/api/os_sys_calls_impl.h"

#define DWORD_MAX UINT32_MAX

namespace Envoy {
namespace Api {
namespace {

using WSAMSGPtr = std::unique_ptr<WSAMSG>;

struct wsamsgResult {
  WSAMSGPtr wsamsg_;
  std::vector<WSABUF> buff_data_;
};

std::vector<WSABUF> iovecToWSABUF(const iovec* vec, int in_vec) {

  DWORD num_vec = 0;
  for (int i = 0; i < in_vec; i++) {
    size_t cur_len = vec[i].iov_len;
    num_vec++;
    while (cur_len > DWORD_MAX) {
      num_vec++;
      cur_len -= DWORD_MAX;
    }
  }

  std::vector<WSABUF> buff(num_vec);
  auto it = buff.begin();

  std::vector<iovec> vecs(vec, vec + in_vec);
  for (const auto& vec : vecs) {
    auto chunk = (CHAR*)vec.iov_base;
    size_t chunk_len = vec.iov_len;
    // There is the case that the chunk does not fit into a single WSABUF buffer
    // this is the case because sizeof(size_t) > sizeof(DWORD).
    // In this case we split the chunk into multiple WSABUF buffers
    auto remaining_data = chunk_len;
    do {
      (*it).buf = chunk;
      (*it).len = (remaining_data > DWORD_MAX) ? DWORD_MAX : static_cast<ULONG>(chunk_len);
      remaining_data -= (*it).len;
      chunk += (*it).len;
      it++;
    } while (remaining_data > 0);
  }
  return buff;
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

wsamsgResult msghdrToWSAMSG(const msghdr* msg) {
  WSAMSGPtr wsa_msg(new WSAMSG);

  wsa_msg->name = reinterpret_cast<SOCKADDR*>(msg->msg_name);
  wsa_msg->namelen = msg->msg_namelen;
  auto buffer = iovecToWSABUF(msg->msg_iov, msg->msg_iovlen);
  wsa_msg->lpBuffers = buffer.data();
  wsa_msg->dwBufferCount = buffer.size();

  WSABUF control;
  control.buf = reinterpret_cast<CHAR*>(msg->msg_control);
  control.len = msg->msg_controllen;
  wsa_msg->Control = control;
  wsa_msg->dwFlags = msg->msg_flags;

  return wsamsgResult{std::move(wsa_msg), std::move(buffer)};
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

SysCallIntResult OsSysCallsImpl::ioctl(os_fd_t sockfd, unsigned long control_code, void* in_buffer,
                                       unsigned long in_buffer_len, void* out_buffer,
                                       unsigned long out_buffer_len,
                                       unsigned long* bytes_returned) {
  const int rc = ::WSAIoctl(sockfd, control_code, in_buffer, in_buffer_len, out_buffer,
                            out_buffer_len, bytes_returned, nullptr, nullptr);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallIntResult OsSysCallsImpl::close(os_fd_t fd) {
  const int rc = ::closesocket(fd);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallSizeResult OsSysCallsImpl::writev(os_fd_t fd, const iovec* iov, int num_iov) {
  DWORD bytes_sent;
  auto buffer = iovecToWSABUF(iov, num_iov);

  const int rc = ::WSASend(fd, buffer.data(), buffer.size(), &bytes_sent, 0, nullptr, nullptr);
  if (SOCKET_FAILURE(rc)) {
    return {-1, ::WSAGetLastError()};
  }
  return {bytes_sent, 0};
}

SysCallSizeResult OsSysCallsImpl::readv(os_fd_t fd, const iovec* iov, int num_iov) {
  DWORD bytes_received;
  DWORD flags = 0;
  auto buffer = iovecToWSABUF(iov, num_iov);

  const int rc =
      ::WSARecv(fd, buffer.data(), buffer.size(), &bytes_received, &flags, nullptr, nullptr);
  if (SOCKET_FAILURE(rc)) {
    return {-1, ::WSAGetLastError()};
  }
  return {bytes_received, 0};
}

SysCallSizeResult OsSysCallsImpl::pwrite(os_fd_t fd, const void* buffer, size_t length,
                                         off_t offset) const {
  PANIC("not implemented");
}

SysCallSizeResult OsSysCallsImpl::pread(os_fd_t fd, void* buffer, size_t length,
                                        off_t offset) const {
  PANIC("not implemented");
}

SysCallSizeResult OsSysCallsImpl::recv(os_fd_t socket, void* buffer, size_t length, int flags) {
  const ssize_t rc = ::recv(socket, static_cast<char*>(buffer), length, flags);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallSizeResult OsSysCallsImpl::recvmsg(os_fd_t sockfd, msghdr* msg, int flags) {
  DWORD bytes_received = 0;
  LPFN_WSARECVMSG recvmsg_fn_ptr = getFnPtrWSARecvMsg();
  wsamsgResult wsamsg = msghdrToWSAMSG(msg);
  // Windows supports only a single flag on input to WSARecvMsg
  wsamsg.wsamsg_->dwFlags = flags & MSG_PEEK;
  const int rc = recvmsg_fn_ptr(sockfd, wsamsg.wsamsg_.get(), &bytes_received, nullptr, nullptr);
  if (rc == SOCKET_ERROR) {
    // We try to match the UNIX behavior for truncated packages. In that case the return code is
    // the length of the allocated buffer and we get the value from `dwFlags`.
    auto last_error = ::WSAGetLastError();
    if (last_error == WSAEMSGSIZE) {
      msg->msg_flags = wsamsg.wsamsg_->dwFlags;
      return {bytes_received, 0};
    }

    return {rc, last_error};
  }
  msg->msg_namelen = wsamsg.wsamsg_->namelen;
  msg->msg_flags = wsamsg.wsamsg_->dwFlags;
  msg->msg_controllen = wsamsg.wsamsg_->Control.len;
  return {bytes_received, 0};
}

SysCallIntResult OsSysCallsImpl::recvmmsg(os_fd_t sockfd, struct mmsghdr* msgvec, unsigned int vlen,
                                          int flags, struct timespec* timeout) {
  PANIC("not implemented");
}

bool OsSysCallsImpl::supportsMmsg() const {
  // Windows doesn't support it.
  return false;
}

bool OsSysCallsImpl::supportsUdpGro() const {
  // Windows doesn't support it.
  return false;
}

bool OsSysCallsImpl::supportsUdpGso() const {
  // Windows doesn't support it.
  return false;
}

bool OsSysCallsImpl::supportsIpTransparent() const {
  // Windows doesn't support it.
  return false;
}

bool OsSysCallsImpl::supportsMptcp() const {
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
  wsamsgResult wsamsg = msghdrToWSAMSG(msg);
  const int rc =
      ::WSASendMsg(sockfd, wsamsg.wsamsg_.get(), flags, &bytes_received, nullptr, nullptr);
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

SysCallIntResult OsSysCallsImpl::open(const char* pathname, int flags) const {
  const int rc = ::open(pathname, flags);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallIntResult OsSysCallsImpl::open(const char* pathname, int flags, mode_t mode) const {
  const int rc = ::open(pathname, flags, mode);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallIntResult OsSysCallsImpl::unlink(const char* pathname) const {
  const int rc = ::unlink(pathname);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallIntResult OsSysCallsImpl::linkat(os_fd_t olddirfd, const char* oldpath, os_fd_t newdirfd,
                                        const char* newpath, int flags) const {
  PANIC("not implemented");
}

SysCallIntResult OsSysCallsImpl::mkstemp(char* tmplate) const { PANIC("not implemented"); }

bool OsSysCallsImpl::supportsAllPosixFileOperations() const { return false; }

SysCallIntResult OsSysCallsImpl::shutdown(os_fd_t sockfd, int how) {
  const int rc = ::shutdown(sockfd, how);
  return {rc, rc != -1 ? 0 : ::WSAGetLastError()};
}

SysCallIntResult OsSysCallsImpl::socketpair(int domain, int type, int protocol, os_fd_t sv[2]) {
  if (sv == nullptr) {
    return {SOCKET_ERROR, SOCKET_ERROR_INVAL};
  }

  sv[0] = sv[1] = INVALID_SOCKET;

  SysCallSocketResult socket_result = socket(domain, type, protocol);
  if (SOCKET_INVALID(socket_result.return_value_)) {
    return {SOCKET_ERROR, socket_result.errno_};
  }

  os_fd_t listener = socket_result.return_value_;

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
    return {SOCKET_ERROR, SOCKET_ERROR_INVAL};
  }

  auto onErr = [this, listener, sv]() -> void {
    ::closesocket(listener);
    ::closesocket(sv[0]);
    ::closesocket(sv[1]);
    sv[0] = INVALID_SOCKET;
    sv[1] = INVALID_SOCKET;
  };

  SysCallIntResult int_result = bind(listener, reinterpret_cast<sockaddr*>(&a), sa_size);
  if (int_result.return_value_ == SOCKET_ERROR) {
    onErr();
    return int_result;
  }

  int_result = listen(listener, 1);
  if (int_result.return_value_ == SOCKET_ERROR) {
    onErr();
    return int_result;
  }

  socket_result = socket(domain, type, protocol);
  if (SOCKET_INVALID(socket_result.return_value_)) {
    onErr();
    return {SOCKET_ERROR, socket_result.errno_};
  }
  sv[0] = socket_result.return_value_;

  a = {};
  int_result = getsockname(listener, reinterpret_cast<sockaddr*>(&a), &sa_size);
  if (int_result.return_value_ == SOCKET_ERROR) {
    onErr();
    return int_result;
  }

  int_result = connect(sv[0], reinterpret_cast<sockaddr*>(&a), sa_size);
  if (int_result.return_value_ == SOCKET_ERROR) {
    onErr();
    return int_result;
  }

  socket_result.return_value_ = ::accept(listener, nullptr, nullptr);
  if (SOCKET_INVALID(socket_result.return_value_)) {
    socket_result.errno_ = ::WSAGetLastError();
    onErr();
    return {SOCKET_ERROR, socket_result.errno_};
  }
  sv[1] = socket_result.return_value_;

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

SysCallSocketResult OsSysCallsImpl::duplicate(os_fd_t oldfd) {
  WSAPROTOCOL_INFO info;
  auto currentProcess = ::GetCurrentProcessId();
  auto rc = WSADuplicateSocket(oldfd, currentProcess, &info);
  if (rc == SOCKET_ERROR) {
    return {(SOCKET)-1, ::WSAGetLastError()};
  }
  auto new_socket = ::WSASocket(info.iAddressFamily, info.iSocketType, info.iProtocol, &info, 0, 0);
  return {new_socket, SOCKET_VALID(new_socket) ? 0 : ::WSAGetLastError()};
}

SysCallSocketResult OsSysCallsImpl::accept(os_fd_t sockfd, sockaddr* addr, socklen_t* addrlen) {
  const os_fd_t rc = ::accept(sockfd, addr, addrlen);
  if (SOCKET_INVALID(rc)) {
    return {rc, ::WSAGetLastError()};
  }

  setsocketblocking(rc, false);
  return {rc, 0};
}

SysCallBoolResult OsSysCallsImpl::socketTcpInfo([[maybe_unused]] os_fd_t sockfd,
                                                [[maybe_unused]] EnvoyTcpInfo* tcp_info) {
#ifdef SIO_TCP_INFO
  TCP_INFO_v0 win_tcpinfo;
  DWORD infoVersion = 0;
  DWORD bytesReturned = 0;
  int rc = ::WSAIoctl(sockfd, SIO_TCP_INFO, &infoVersion, sizeof(infoVersion), &win_tcpinfo,
                      sizeof(win_tcpinfo), &bytesReturned, nullptr, nullptr);

  if (!SOCKET_FAILURE(rc)) {
    tcp_info->tcpi_rtt = std::chrono::microseconds(win_tcpinfo.RttUs);
    tcp_info->tcpi_snd_cwnd = win_tcpinfo.Cwnd;
  }
  return {!SOCKET_FAILURE(rc), !SOCKET_FAILURE(rc) ? 0 : ::WSAGetLastError()};
#endif
  return {false, WSAEOPNOTSUPP};
}

bool OsSysCallsImpl::supportsGetifaddrs() const {
  if (alternate_getifaddrs_.has_value()) {
    return true;
  }
  return false;
}

SysCallIntResult OsSysCallsImpl::getifaddrs([[maybe_unused]] InterfaceAddressVector& interfaces) {
  if (alternate_getifaddrs_.has_value()) {
    return alternate_getifaddrs_.value()(interfaces);
  }
  PANIC("not implemented");
}

SysCallIntResult OsSysCallsImpl::getaddrinfo(const char* node, const char* service,
                                             const addrinfo* hints, addrinfo** res) {
  const int rc = ::getaddrinfo(node, service, hints, res);
  return {rc, errno};
}

void OsSysCallsImpl::freeaddrinfo(addrinfo* res) { ::freeaddrinfo(res); }

} // namespace Api
} // namespace Envoy
