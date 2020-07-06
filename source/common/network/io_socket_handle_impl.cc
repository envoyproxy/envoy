#include "common/network/io_socket_handle_impl.h"

#include "envoy/buffer/buffer.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/utility.h"
#include "common/network/address_impl.h"

#include "absl/container/fixed_array.h"
#include "absl/types/optional.h"

using Envoy::Api::SysCallIntResult;
using Envoy::Api::SysCallSizeResult;

namespace Envoy {
namespace Network {

IoSocketHandleImpl::~IoSocketHandleImpl() {
  if (SOCKET_VALID(fd_)) {
    IoSocketHandleImpl::close();
  }
}

Api::IoCallUint64Result IoSocketHandleImpl::close() {
  ASSERT(SOCKET_VALID(fd_));
  const int rc = Api::OsSysCallsSingleton::get().close(fd_).rc_;
  SET_SOCKET_INVALID(fd_);
  return Api::IoCallUint64Result(rc, Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError));
}

bool IoSocketHandleImpl::isOpen() const { return SOCKET_VALID(fd_); }

Api::IoCallUint64Result IoSocketHandleImpl::readv(uint64_t max_length, Buffer::RawSlice* slices,
                                                  uint64_t num_slice) {
  absl::FixedArray<iovec> iov(num_slice);
  uint64_t num_slices_to_read = 0;
  uint64_t num_bytes_to_read = 0;
  for (; num_slices_to_read < num_slice && num_bytes_to_read < max_length; num_slices_to_read++) {
    iov[num_slices_to_read].iov_base = slices[num_slices_to_read].mem_;
    const size_t slice_length = std::min(slices[num_slices_to_read].len_,
                                         static_cast<size_t>(max_length - num_bytes_to_read));
    iov[num_slices_to_read].iov_len = slice_length;
    num_bytes_to_read += slice_length;
  }
  ASSERT(num_bytes_to_read <= max_length);
  return sysCallResultToIoCallResult(Api::OsSysCallsSingleton::get().readv(
      fd_, iov.begin(), static_cast<int>(num_slices_to_read)));
}

Api::IoCallUint64Result IoSocketHandleImpl::writev(const Buffer::RawSlice* slices,
                                                   uint64_t num_slice) {
  absl::FixedArray<iovec> iov(num_slice);
  uint64_t num_slices_to_write = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      iov[num_slices_to_write].iov_base = slices[i].mem_;
      iov[num_slices_to_write].iov_len = slices[i].len_;
      num_slices_to_write++;
    }
  }
  if (num_slices_to_write == 0) {
    return Api::ioCallUint64ResultNoError();
  }
  return sysCallResultToIoCallResult(
      Api::OsSysCallsSingleton::get().writev(fd_, iov.begin(), num_slices_to_write));
}

Api::IoCallUint64Result IoSocketHandleImpl::sendmsg(const Buffer::RawSlice* slices,
                                                    uint64_t num_slice, int flags,
                                                    const Address::Ip* self_ip,
                                                    const Address::Instance& peer_address) {
  const auto* address_base = dynamic_cast<const Address::InstanceBase*>(&peer_address);
  sockaddr* sock_addr = const_cast<sockaddr*>(address_base->sockAddr());

  absl::FixedArray<iovec> iov(num_slice);
  uint64_t num_slices_to_write = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      iov[num_slices_to_write].iov_base = slices[i].mem_;
      iov[num_slices_to_write].iov_len = slices[i].len_;
      num_slices_to_write++;
    }
  }
  if (num_slices_to_write == 0) {
    return Api::ioCallUint64ResultNoError();
  }

  msghdr message;
  message.msg_name = reinterpret_cast<void*>(sock_addr);
  message.msg_namelen = address_base->sockAddrLen();
  message.msg_iov = iov.begin();
  message.msg_iovlen = num_slices_to_write;
  message.msg_flags = 0;
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  if (self_ip == nullptr) {
    message.msg_control = nullptr;
    message.msg_controllen = 0;
    const Api::SysCallSizeResult result = os_syscalls.sendmsg(fd_, &message, flags);
    return sysCallResultToIoCallResult(result);
  } else {
    const size_t space_v6 = CMSG_SPACE(sizeof(in6_pktinfo));
    // FreeBSD only needs in_addr size, but allocates more to unify code in two platforms.
    const size_t space_v4 = CMSG_SPACE(sizeof(in_pktinfo));
    const size_t cmsg_space = (space_v4 < space_v6) ? space_v6 : space_v4;
    // kSpaceForIp should be big enough to hold both IPv4 and IPv6 packet info.
    absl::FixedArray<char> cbuf(cmsg_space);
    memset(cbuf.begin(), 0, cmsg_space);

    message.msg_control = cbuf.begin();
    message.msg_controllen = cmsg_space * sizeof(char);
    cmsghdr* const cmsg = CMSG_FIRSTHDR(&message);
    RELEASE_ASSERT(cmsg != nullptr, fmt::format("cbuf with size {} is not enough, cmsghdr size {}",
                                                sizeof(cbuf), sizeof(cmsghdr)));
    if (self_ip->version() == Address::IpVersion::v4) {
      cmsg->cmsg_level = IPPROTO_IP;
#ifndef IP_SENDSRCADDR
      cmsg->cmsg_len = CMSG_LEN(sizeof(in_pktinfo));
      cmsg->cmsg_type = IP_PKTINFO;
      auto pktinfo = reinterpret_cast<in_pktinfo*>(CMSG_DATA(cmsg));
      pktinfo->ipi_ifindex = 0;
#ifdef WIN32
      pktinfo->ipi_addr.s_addr = self_ip->ipv4()->address();
#else
      pktinfo->ipi_spec_dst.s_addr = self_ip->ipv4()->address();
#endif
#else
      cmsg->cmsg_type = IP_SENDSRCADDR;
      cmsg->cmsg_len = CMSG_LEN(sizeof(in_addr));
      *(reinterpret_cast<struct in_addr*>(CMSG_DATA(cmsg))).s_addr = self_ip->ipv4()->address();
#endif
    } else if (self_ip->version() == Address::IpVersion::v6) {
      cmsg->cmsg_len = CMSG_LEN(sizeof(in6_pktinfo));
      cmsg->cmsg_level = IPPROTO_IPV6;
      cmsg->cmsg_type = IPV6_PKTINFO;
      auto pktinfo = reinterpret_cast<in6_pktinfo*>(CMSG_DATA(cmsg));
      pktinfo->ipi6_ifindex = 0;
      *(reinterpret_cast<absl::uint128*>(pktinfo->ipi6_addr.s6_addr)) = self_ip->ipv6()->address();
    }
    const Api::SysCallSizeResult result = os_syscalls.sendmsg(fd_, &message, flags);
    return sysCallResultToIoCallResult(result);
  }
}

Address::InstanceConstSharedPtr getAddressFromSockAddrOrDie(const sockaddr_storage& ss,
                                                            socklen_t ss_len, os_fd_t fd) {
  try {
    // Set v6only to false so that mapped-v6 address can be normalize to v4
    // address. Though dual stack may be disabled, it's still okay to assume the
    // address is from a dual stack socket. This is because mapped-v6 address
    // must come from a dual stack socket. An actual v6 address can come from
    // both dual stack socket and v6 only socket. If |peer_addr| is an actual v6
    // address and the socket is actually v6 only, the returned address will be
    // regarded as a v6 address from dual stack socket. However, this address is not going to be
    // used to create socket. Wrong knowledge of dual stack support won't hurt.
    return Address::addressFromSockAddr(ss, ss_len, /*v6only=*/false);
  } catch (const EnvoyException& e) {
    PANIC(fmt::format("Invalid address for fd: {}, error: {}", fd, e.what()));
  }
}

Address::InstanceConstSharedPtr maybeGetDstAddressFromHeader(const cmsghdr& cmsg,
                                                             uint32_t self_port, os_fd_t fd) {
  if (cmsg.cmsg_type == IPV6_PKTINFO) {
    auto info = reinterpret_cast<const in6_pktinfo*>(CMSG_DATA(&cmsg));
    sockaddr_storage ss;
    auto ipv6_addr = reinterpret_cast<sockaddr_in6*>(&ss);
    memset(ipv6_addr, 0, sizeof(sockaddr_in6));
    ipv6_addr->sin6_family = AF_INET6;
    ipv6_addr->sin6_addr = info->ipi6_addr;
    ipv6_addr->sin6_port = htons(self_port);
    return getAddressFromSockAddrOrDie(ss, sizeof(sockaddr_in6), fd);
  }
#ifndef IP_RECVDSTADDR
  if (cmsg.cmsg_type == IP_PKTINFO) {
    auto info = reinterpret_cast<const in_pktinfo*>(CMSG_DATA(&cmsg));
#else
  if (cmsg.cmsg_type == IP_RECVDSTADDR) {
    auto addr = reinterpret_cast<const in_addr*>(CMSG_DATA(&cmsg));
#endif
    sockaddr_storage ss;
    auto ipv4_addr = reinterpret_cast<sockaddr_in*>(&ss);
    memset(ipv4_addr, 0, sizeof(sockaddr_in));
    ipv4_addr->sin_family = AF_INET;
    ipv4_addr->sin_addr =
#ifndef IP_RECVDSTADDR
        info->ipi_addr;
#else
        *addr;
#endif
    ipv4_addr->sin_port = htons(self_port);
    return getAddressFromSockAddrOrDie(ss, sizeof(sockaddr_in), fd);
  }
  return nullptr;
}

absl::optional<uint32_t> maybeGetPacketsDroppedFromHeader(
#ifdef SO_RXQ_OVFL
    const cmsghdr& cmsg) {
  if (cmsg.cmsg_type == SO_RXQ_OVFL) {
    return *reinterpret_cast<const uint32_t*>(CMSG_DATA(&cmsg));
  }
#else
    const cmsghdr&) {
#endif
  return absl::nullopt;
}

Api::IoCallUint64Result IoSocketHandleImpl::recvmsg(Buffer::RawSlice* slices,
                                                    const uint64_t num_slice, uint32_t self_port,
                                                    RecvMsgOutput& output) {
  ASSERT(!output.msg_.empty());

  absl::FixedArray<char> cbuf(cmsg_space_);
  memset(cbuf.begin(), 0, cmsg_space_);

  absl::FixedArray<iovec> iov(num_slice);
  uint64_t num_slices_for_read = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      iov[num_slices_for_read].iov_base = slices[i].mem_;
      iov[num_slices_for_read].iov_len = slices[i].len_;
      ++num_slices_for_read;
    }
  }
  if (num_slices_for_read == 0) {
    return Api::ioCallUint64ResultNoError();
  }

  sockaddr_storage peer_addr;
  msghdr hdr;
  hdr.msg_name = &peer_addr;
  hdr.msg_namelen = sizeof(sockaddr_storage);
  hdr.msg_iov = iov.begin();
  hdr.msg_iovlen = num_slices_for_read;
  hdr.msg_flags = 0;
  hdr.msg_control = cbuf.begin();
  hdr.msg_controllen = cmsg_space_;
  const Api::SysCallSizeResult result = Api::OsSysCallsSingleton::get().recvmsg(fd_, &hdr, 0);
  if (result.rc_ < 0) {
    return sysCallResultToIoCallResult(result);
  }

  RELEASE_ASSERT((hdr.msg_flags & MSG_CTRUNC) == 0,
                 fmt::format("Incorrectly set control message length: {}", hdr.msg_controllen));
  RELEASE_ASSERT(hdr.msg_namelen > 0,
                 fmt::format("Unable to get remote address from recvmsg() for fd: {}", fd_));
  output.msg_[0].peer_address_ = getAddressFromSockAddrOrDie(peer_addr, hdr.msg_namelen, fd_);

  if (hdr.msg_controllen > 0) {
    // Get overflow, local address from control message.
    for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&hdr); cmsg != nullptr;
         cmsg = CMSG_NXTHDR(&hdr, cmsg)) {
      if (output.msg_[0].local_address_ == nullptr) {
        Address::InstanceConstSharedPtr addr = maybeGetDstAddressFromHeader(*cmsg, self_port, fd_);
        if (addr != nullptr) {
          // This is a IP packet info message.
          output.msg_[0].local_address_ = std::move(addr);
          continue;
        }
      }
      if (output.dropped_packets_ != nullptr) {
        absl::optional<uint32_t> maybe_dropped = maybeGetPacketsDroppedFromHeader(*cmsg);
        if (maybe_dropped) {
          *output.dropped_packets_ = *maybe_dropped;
        }
      }
    }
  }
  return sysCallResultToIoCallResult(result);
}

Api::IoCallUint64Result IoSocketHandleImpl::recvmmsg(RawSliceArrays& slices, uint32_t self_port,
                                                     RecvMsgOutput& output) {
  ASSERT(output.msg_.size() == slices.size());
  if (slices.empty()) {
    return sysCallResultToIoCallResult(Api::SysCallIntResult{0, SOCKET_ERROR_AGAIN});
  }
  const uint32_t num_packets_per_mmsg_call = slices.size();
  absl::FixedArray<mmsghdr> mmsg_hdr(num_packets_per_mmsg_call);
  absl::FixedArray<absl::FixedArray<struct iovec>> iovs(
      num_packets_per_mmsg_call, absl::FixedArray<struct iovec>(slices[0].size()));
  absl::FixedArray<sockaddr_storage> raw_addresses(num_packets_per_mmsg_call);
  absl::FixedArray<absl::FixedArray<char>> cbufs(num_packets_per_mmsg_call,
                                                 absl::FixedArray<char>(cmsg_space_));

  for (uint32_t i = 0; i < num_packets_per_mmsg_call; ++i) {
    memset(&raw_addresses[i], 0, sizeof(sockaddr_storage));
    memset(cbufs[i].data(), 0, cbufs[i].size());

    mmsg_hdr[i].msg_len = 0;

    msghdr* hdr = &mmsg_hdr[i].msg_hdr;
    hdr->msg_name = &raw_addresses[i];
    hdr->msg_namelen = sizeof(sockaddr_storage);
    ASSERT(!slices[i].empty());

    for (size_t j = 0; j < slices[i].size(); ++j) {
      iovs[i][j].iov_base = slices[i][j].mem_;
      iovs[i][j].iov_len = slices[i][j].len_;
    }
    hdr->msg_iov = iovs[i].data();
    hdr->msg_iovlen = slices[i].size();
    hdr->msg_control = cbufs[i].data();
    hdr->msg_controllen = cbufs[i].size();
  }

  // Set MSG_WAITFORONE so that recvmmsg will not waiting for
  // |num_packets_per_mmsg_call| packets to arrive before returning when the
  // socket is a blocking socket.
  const Api::SysCallIntResult result = Api::OsSysCallsSingleton::get().recvmmsg(
      fd_, mmsg_hdr.data(), num_packets_per_mmsg_call, MSG_TRUNC | MSG_WAITFORONE, nullptr);

  if (result.rc_ <= 0) {
    return sysCallResultToIoCallResult(result);
  }

  int num_packets_read = result.rc_;

  for (int i = 0; i < num_packets_read; ++i) {
    if (mmsg_hdr[i].msg_len == 0) {
      continue;
    }
    msghdr& hdr = mmsg_hdr[i].msg_hdr;
    RELEASE_ASSERT((hdr.msg_flags & MSG_CTRUNC) == 0,
                   fmt::format("Incorrectly set control message length: {}", hdr.msg_controllen));
    RELEASE_ASSERT(hdr.msg_namelen > 0,
                   fmt::format("Unable to get remote address from recvmmsg() for fd: {}", fd_));
    if ((hdr.msg_flags & MSG_TRUNC) != 0) {
      ENVOY_LOG_MISC(warn, "Dropping truncated UDP packet with size: {}.", mmsg_hdr[i].msg_len);
      continue;
    }

    output.msg_[i].msg_len_ = mmsg_hdr[i].msg_len;
    // Get local and peer addresses for each packet.
    output.msg_[i].peer_address_ =
        getAddressFromSockAddrOrDie(raw_addresses[i], hdr.msg_namelen, fd_);
    if (hdr.msg_controllen > 0) {
      struct cmsghdr* cmsg;
      for (cmsg = CMSG_FIRSTHDR(&hdr); cmsg != nullptr; cmsg = CMSG_NXTHDR(&hdr, cmsg)) {
        Address::InstanceConstSharedPtr addr = maybeGetDstAddressFromHeader(*cmsg, self_port, fd_);
        if (addr != nullptr) {
          // This is a IP packet info message.
          output.msg_[i].local_address_ = std::move(addr);
          break;
        }
      }
    }
  }
  // Get overflow from first packet header.
  if (output.dropped_packets_ != nullptr) {
    msghdr& hdr = mmsg_hdr[0].msg_hdr;
    if (hdr.msg_controllen > 0) {
      struct cmsghdr* cmsg;
      for (cmsg = CMSG_FIRSTHDR(&hdr); cmsg != nullptr; cmsg = CMSG_NXTHDR(&hdr, cmsg)) {
        absl::optional<uint32_t> maybe_dropped = maybeGetPacketsDroppedFromHeader(*cmsg);
        if (maybe_dropped) {
          *output.dropped_packets_ = *maybe_dropped;
        }
      }
    }
  }
  return sysCallResultToIoCallResult(result);
}

bool IoSocketHandleImpl::supportsMmsg() const {
  return Api::OsSysCallsSingleton::get().supportsMmsg();
}

Api::SysCallIntResult IoSocketHandleImpl::bind(Address::InstanceConstSharedPtr address) {
  return Api::OsSysCallsSingleton::get().bind(fd_, address->sockAddr(), address->sockAddrLen());
}

Api::SysCallIntResult IoSocketHandleImpl::listen(int backlog) {
  return Api::OsSysCallsSingleton::get().listen(fd_, backlog);
}

Api::SysCallIntResult IoSocketHandleImpl::connect(Address::InstanceConstSharedPtr address) {
  return Api::OsSysCallsSingleton::get().connect(fd_, address->sockAddr(), address->sockAddrLen());
}

Api::SysCallIntResult IoSocketHandleImpl::setOption(int level, int optname, const void* optval,
                                                    socklen_t optlen) {
  return Api::OsSysCallsSingleton::get().setsockopt(fd_, level, optname, optval, optlen);
}

Api::SysCallIntResult IoSocketHandleImpl::getOption(int level, int optname, void* optval,
                                                    socklen_t* optlen) {
  return Api::OsSysCallsSingleton::get().getsockopt(fd_, level, optname, optval, optlen);
}

Api::SysCallIntResult IoSocketHandleImpl::setBlocking(bool blocking) {
  return Api::OsSysCallsSingleton::get().setsocketblocking(fd_, blocking);
}

absl::optional<int> IoSocketHandleImpl::domain() {
  sockaddr_storage addr;
  socklen_t len = sizeof(addr);
  Api::SysCallIntResult result;

  result = Api::OsSysCallsSingleton::get().getsockname(
      fd_, reinterpret_cast<struct sockaddr*>(&addr), &len);

  if (result.rc_ == 0) {
    return {addr.ss_family};
  }

  return absl::nullopt;
}

Address::InstanceConstSharedPtr IoSocketHandleImpl::localAddress() {
  sockaddr_storage ss;
  socklen_t ss_len = sizeof(ss);
  auto& os_sys_calls = Api::OsSysCallsSingleton::get();
  Api::SysCallIntResult result =
      os_sys_calls.getsockname(fd_, reinterpret_cast<sockaddr*>(&ss), &ss_len);
  if (result.rc_ != 0) {
    throw EnvoyException(fmt::format("getsockname failed for '{}': ({}) {}", fd_, result.errno_,
                                     errorDetails(result.errno_)));
  }
  int socket_v6only = 0;
  if (ss.ss_family == AF_INET6) {
    socklen_t size_int = sizeof(socket_v6only);
    result = os_sys_calls.getsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, &socket_v6only, &size_int);
#ifdef WIN32
    // On Windows, it is possible for this getsockopt() call to fail.
    // This can happen if the address we are trying to connect to has nothing
    // listening. So we can't use RELEASE_ASSERT and instead must throw an
    // exception
    if (SOCKET_FAILURE(result.rc_)) {
      throw EnvoyException(fmt::format("getsockopt failed for '{}': ({}) {}", fd_, result.errno_,
                                       errorDetails(result.errno_)));
    }
#else
    RELEASE_ASSERT(result.rc_ == 0, "");
#endif
  }
  return Address::addressFromSockAddr(ss, ss_len, socket_v6only);
}

Address::InstanceConstSharedPtr IoSocketHandleImpl::peerAddress() {
  sockaddr_storage ss;
  socklen_t ss_len = sizeof ss;
  auto& os_sys_calls = Api::OsSysCallsSingleton::get();
  Api::SysCallIntResult result =
      os_sys_calls.getpeername(fd_, reinterpret_cast<sockaddr*>(&ss), &ss_len);
  if (result.rc_ != 0) {
    throw EnvoyException(
        fmt::format("getpeername failed for '{}': {}", fd_, errorDetails(result.errno_)));
  }
#ifdef __APPLE__
  if (ss_len == sizeof(sockaddr) && ss.ss_family == AF_UNIX)
#else
  if (ss_len == sizeof(sa_family_t) && ss.ss_family == AF_UNIX)
#endif
  {
    // For Unix domain sockets, can't find out the peer name, but it should match our own
    // name for the socket (i.e. the path should match, barring any namespace or other
    // mechanisms to hide things, of which there are many).
    ss_len = sizeof ss;
    result = os_sys_calls.getsockname(fd_, reinterpret_cast<sockaddr*>(&ss), &ss_len);
    if (result.rc_ != 0) {
      throw EnvoyException(
          fmt::format("getsockname failed for '{}': {}", fd_, errorDetails(result.errno_)));
    }
  }
  return Address::addressFromSockAddr(ss, ss_len);
}

} // namespace Network
} // namespace Envoy
