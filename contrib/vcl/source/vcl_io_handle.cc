#include "contrib/vcl/source/vcl_io_handle.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"

#include "contrib/vcl/source/vcl_event.h"
#include "contrib/vcl/source/vcl_interface.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace Vcl {

namespace {

int vclWrkIndexOrRegister() {
  int wrk_index = vppcom_worker_index();
  if (wrk_index == -1) {
    vclInterfaceWorkerRegister();
    wrk_index = vppcom_worker_index();
  }
  return wrk_index;
}

bool peekVclSession(vcl_session_handle_t sh, vppcom_endpt_t* ep, uint32_t* proto) {
  int current_wrk = vppcom_worker_index();
  int sh_wrk = vppcom_session_worker(sh);
  uint32_t eplen = sizeof(*ep);

  // should NOT be used while system is loaded
  vppcom_worker_index_set(sh_wrk);

  if (vppcom_session_attr(sh, VPPCOM_ATTR_GET_LCL_ADDR, ep, &eplen) != VPPCOM_OK) {
    return true;
  }

  uint32_t buflen = sizeof(uint32_t);
  if (vppcom_session_attr(sh, VPPCOM_ATTR_GET_PROTOCOL, proto, &buflen) != VPPCOM_OK) {
    return true;
  }

  vppcom_worker_index_set(current_wrk);

  return false;
}

void vclEndptCopy(sockaddr* addr, socklen_t* addrlen, const vppcom_endpt_t& ep) {
  if (ep.is_ip4) {
    sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(addr);
    addr4->sin_family = AF_INET;
    *addrlen = std::min(static_cast<unsigned int>(sizeof(struct sockaddr_in)), *addrlen);
    memcpy(&addr4->sin_addr, ep.ip, *addrlen); // NOLINT(safe-memcpy)
    addr4->sin_port = ep.port;
  } else {
    sockaddr_in6* addr6 = reinterpret_cast<sockaddr_in6*>(addr);
    addr6->sin6_family = AF_INET6;
    *addrlen = std::min(static_cast<unsigned int>(sizeof(struct sockaddr_in6)), *addrlen);
    memcpy(&addr6->sin6_addr, ep.ip, *addrlen); // NOLINT(safe-memcpy)
    addr6->sin6_port = ep.port;
  }
}

Envoy::Network::Address::InstanceConstSharedPtr vclEndptToAddress(const vppcom_endpt_t& ep,
                                                                  uint32_t sh) {
  sockaddr_storage addr;
  int len;

  if (ep.is_ip4) {
    addr.ss_family = AF_INET;
    len = sizeof(struct sockaddr_in);
    auto in4 = reinterpret_cast<struct sockaddr_in*>(&addr);
    memcpy(&in4->sin_addr, ep.ip, len); // NOLINT(safe-memcpy)
    in4->sin_port = ep.port;
  } else {
    addr.ss_family = AF_INET6;
    len = sizeof(struct sockaddr_in6);
    auto in6 = reinterpret_cast<struct sockaddr_in6*>(&addr);
    memcpy(&in6->sin6_addr, ep.ip, len); // NOLINT(safe-memcpy)
    in6->sin6_port = ep.port;
  }

  try {
    // Set v6only to false so that mapped-v6 address can be normalize to v4
    // address. Though dual stack may be disabled, it's still okay to assume the
    // address is from a dual stack socket. This is because mapped-v6 address
    // must come from a dual stack socket. An actual v6 address can come from
    // both dual stack socket and v6 only socket. If |peer_addr| is an actual v6
    // address and the socket is actually v6 only, the returned address will be
    // regarded as a v6 address from dual stack socket. However, this address is not going to be
    // used to create socket. Wrong knowledge of dual stack support won't hurt.
    return *Envoy::Network::Address::addressFromSockAddr(addr, len, /*v6only=*/false);
  } catch (const EnvoyException& e) {
    PANIC(fmt::format("Invalid remote address for fd: {}, error: {}", sh, e.what()));
  }
}

void vclEndptFromAddress(vppcom_endpt_t& endpt,
                         Envoy::Network::Address::InstanceConstSharedPtr address) {
  if (address->ip()->version() == Envoy::Network::Address::IpVersion::v4) {
    const sockaddr_in* in = reinterpret_cast<const sockaddr_in*>(address->sockAddr());
    endpt.is_ip4 = 1;
    endpt.ip = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(&in->sin_addr));
    endpt.port = static_cast<uint16_t>(in->sin_port);
  } else {
    const sockaddr_in6* in6 = reinterpret_cast<const sockaddr_in6*>(address->sockAddr());
    endpt.is_ip4 = 0;
    endpt.ip = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(&in6->sin6_addr));
    endpt.port = static_cast<uint16_t>(in6->sin6_port);
  }
}

// Converts a VCL return types to IoCallUint64Result.
Api::IoCallUint64Result vclCallResultToIoCallResult(const int32_t result) {
  if (result >= 0) {
    // Return nullptr as IoError upon success.
    return {static_cast<unsigned long>(result), Api::IoError::none()};
  }
  RELEASE_ASSERT(result != VPPCOM_EINVAL, "Invalid argument passed in.");
  return {/*rc=*/0, (result == VPPCOM_EAGAIN
                         // EAGAIN is frequent enough that its memory allocation should be avoided.
                         ? Envoy::Network::IoSocketError::getIoSocketEagainError()
                         : Envoy::Network::IoSocketError::create(-result))};
}

} // namespace

VclIoHandle::~VclIoHandle() {
  if (VCL_SH_VALID(sh_)) {
    VclIoHandle::close();
  }
}

Api::IoCallUint64Result VclIoHandle::close() {
  int wrk_index = vclWrkIndexOrRegister();
  int rc = 0;

  VCL_LOG("closing sh {:x}", sh_);

  if (!VCL_SH_VALID(sh_)) {
    ENVOY_LOG_MISC(info, "[{}] sh {:x} already closed is_listener {} isWrkListener{}", wrk_index,
                   sh_, is_listener_, isWrkListener());
    return {static_cast<unsigned long>(rc), Api::IoError::none()};
  }

  if (is_listener_) {
    ENVOY_LOG_MISC(info, "[{}] destroying listener sh {}", wrk_index, sh_);
    if (wrk_index) {
      if (wrk_listener_ != nullptr) {
        uint32_t sh = wrk_listener_->sh();
        RELEASE_ASSERT(wrk_index == vppcom_session_worker(sh), "listener close on wrong thread");
      }
      clearChildWrkListener();
      // sh_ not invalidated yet, waiting for destructor on main to call `vppcom_session_close`
    } else {
      clearChildWrkListener();
      rc = vppcom_session_close(sh_);
      VCL_SET_SH_INVALID(sh_);
    }
  } else {
    rc = vppcom_session_close(sh_);
    VCL_SET_SH_INVALID(sh_);
  }

  return {static_cast<unsigned long>(rc), Api::IoError::none()};
}

bool VclIoHandle::isOpen() const { return VCL_SH_VALID(sh_); }

bool VclIoHandle::wasConnected() const { return false; }

Api::IoCallUint64Result VclIoHandle::readv(uint64_t max_length, Buffer::RawSlice* slices,
                                           uint64_t num_slice) {
  if (!VCL_SH_VALID(sh_)) {
    return vclCallResultToIoCallResult(VPPCOM_EBADFD);
  }

  VCL_LOG("reading on sh {:x}", sh_);

  uint64_t num_bytes_read = 0;
  int32_t result = 0, rv = 0;
  size_t slice_length;

  for (uint64_t i = 0; i < num_slice; i++) {
    slice_length = std::min(slices[i].len_, static_cast<size_t>(max_length - num_bytes_read));
    rv = vppcom_session_read(sh_, slices[i].mem_, slice_length);
    if (rv < 0) {
      break;
    }
    num_bytes_read += rv;
    if (static_cast<size_t>(rv) < slice_length || num_bytes_read == max_length) {
      break;
    }
  }
  result = (num_bytes_read == 0) ? rv : num_bytes_read;
  VCL_LOG("done reading on sh {:x} bytes {} result {}", sh_, num_bytes_read, result);
  return vclCallResultToIoCallResult(result);
}

#if VCL_RX_ZC
Api::IoCallUint64Result VclIoHandle::read(Buffer::Instance& buffer, absl::optional<uint64_t>) {
  vppcom_data_segment_t ds[16];
  int32_t rv;

  rv = vppcom_session_read_segments(sh_, ds, 16, ~0);
  if (rv < 0) {
    return vclCallResultToIoCallResult(rv);
  }

  uint32_t ds_index = 0, sh = sh_, len;
  int32_t n_bytes = 0;
  while (n_bytes < rv) {
    len = ds[ds_index].len;
    auto fragment = new Envoy::Buffer::BufferFragmentImpl(
        ds[ds_index].data, len,
        [&, sh](const void*, size_t data_len,
                const Envoy::Buffer::BufferFragmentImpl* this_fragment) {
          vppcom_session_free_segments(sh, data_len);
          delete this_fragment;
        });

    buffer.addBufferFragment(*fragment);
    n_bytes += len;
    ds_index += 1;
  }

  return vclCallResultToIoCallResult(rv);
}
#else
Api::IoCallUint64Result VclIoHandle::read(Buffer::Instance& buffer,
                                          absl::optional<uint64_t> max_length_opt) {
  uint64_t max_length = max_length_opt.value_or(UINT64_MAX);
  if (max_length == 0) {
    return Api::ioCallUint64ResultNoError();
  }

  Buffer::Reservation reservation = buffer.reserveForRead();
  Api::IoCallUint64Result result = readv(std::min(reservation.length(), max_length),
                                         reservation.slices(), reservation.numSlices());
  uint64_t bytes_to_commit = result.ok() ? result.return_value_ : 0;
  ASSERT(bytes_to_commit <= max_length);
  reservation.commit(bytes_to_commit);
  return result;
}
#endif

Api::IoCallUint64Result VclIoHandle::writev(const Buffer::RawSlice* slices, uint64_t num_slice) {
  if (!VCL_SH_VALID(sh_)) {
    return vclCallResultToIoCallResult(VPPCOM_EBADFD);
  }

  VCL_LOG("writing on sh {:x}", sh_);

  uint64_t num_bytes_written = 0;
  int32_t result = 0, rv = 0;

  for (uint64_t i = 0; i < num_slice; i++) {
    rv = vppcom_session_write(sh_, slices[i].mem_, slices[i].len_);
    if (rv < 0) {
      break;
    }
    num_bytes_written += rv;
  }
  result = (num_bytes_written == 0) ? rv : num_bytes_written;

  return vclCallResultToIoCallResult(result);
}

Api::IoCallUint64Result VclIoHandle::write(Buffer::Instance& buffer) {
  constexpr uint64_t MaxSlices = 16;
  Buffer::RawSliceVector slices = buffer.getRawSlices(MaxSlices);
  Api::IoCallUint64Result result = writev(slices.begin(), slices.size());
  if (result.ok() && result.return_value_ > 0) {
    buffer.drain(static_cast<uint64_t>(result.return_value_));
  }
  return result;
}

Api::IoCallUint64Result VclIoHandle::recv(void* buffer, size_t length, int flags) {
  VCL_LOG("recv on sh {:x}", sh_);
  int rv = vppcom_session_recvfrom(sh_, buffer, length, flags, nullptr);
  return vclCallResultToIoCallResult(rv);
}

Api::IoCallUint64Result VclIoHandle::sendmsg(const Buffer::RawSlice* slices, uint64_t num_slice,
                                             int, const Envoy::Network::Address::Ip*,
                                             const Envoy::Network::Address::Instance&) {
  if (!VCL_SH_VALID(sh_)) {
    return vclCallResultToIoCallResult(VPPCOM_EBADFD);
  }
  VCL_LOG("sendmsg called on {:x}", sh_);

  absl::FixedArray<iovec> iov(num_slice);
  uint64_t num_slices_to_write = 0;
  uint64_t num_bytes_written = 0;

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

  // VCL has no sendmsg semantics- Treat as a session write followed by a flush
  int result = 0;
  for (uint64_t i = 0; i < num_slices_to_write; i++) {
    int n;
    if (i < (num_slices_to_write - 1)) {
      n = vppcom_session_write(sh_, iov[i].iov_base, iov[i].iov_len);
      if (n < 0) {
        result = (num_bytes_written == 0) ? n : num_bytes_written;
        break;
      }
    } else {
      // Flush after the last segment is written
      n = vppcom_session_write_msg(sh_, iov[i].iov_base, iov[i].iov_len);
      if (n < 0) {
        result = (num_bytes_written == 0) ? n : num_bytes_written;
        break;
      }
    }
    num_bytes_written += n;
  }

  return vclCallResultToIoCallResult(result);
}

Api::IoCallUint64Result VclIoHandle::recvmsg(Buffer::RawSlice* slices, const uint64_t num_slice,
                                             uint32_t self_port, const UdpSaveCmsgConfig&,
                                             RecvMsgOutput& output) {
  if (!VCL_SH_VALID(sh_)) {
    return vclCallResultToIoCallResult(VPPCOM_EBADFD);
  }

  absl::FixedArray<iovec> iov(num_slice);
  uint64_t num_slices_for_read = 0;
  uint64_t num_bytes_recvd = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      iov[num_slices_for_read].iov_base = slices[i].mem_;
      iov[num_slices_for_read].iov_len = slices[i].len_;
      ++num_slices_for_read;
    }
  }

  // VCL has no recvmsg semantics- treat as a read into each slice, which is not
  // as cumbersome as it sounds, since VCL will simply copy from shared mem buffers
  // if the data is available.
  uint8_t ipaddr[sizeof(absl::uint128)];
  vppcom_endpt_t endpt;
  endpt.ip = ipaddr;
  endpt.port = static_cast<uint16_t>(self_port);
  int result = 0;

  for (uint64_t i = 0; i < num_slices_for_read; i++) {
    int n = vppcom_session_recvfrom(sh_, iov[i].iov_base, iov[i].iov_len, 0, &endpt);
    if (n < 0) {
      result = (num_bytes_recvd == 0) ? n : num_bytes_recvd;
      break;
    }
    if (i == 0) {
      output.msg_[0].peer_address_ = vclEndptToAddress(endpt, sh_);
    }
    num_bytes_recvd += n;
  }

  if (result < 0) {
    return vclCallResultToIoCallResult(result);
  }

  output.dropped_packets_ = nullptr;

  return vclCallResultToIoCallResult(result);
}

Api::IoCallUint64Result VclIoHandle::recvmmsg(RawSliceArrays&, uint32_t, const UdpSaveCmsgConfig&,
                                              RecvMsgOutput&) {
  PANIC("not implemented");
}

bool VclIoHandle::supportsMmsg() const { return false; }

Api::SysCallIntResult VclIoHandle::bind(Envoy::Network::Address::InstanceConstSharedPtr address) {
  if (!VCL_SH_VALID(sh_)) {
    return {-1, VPPCOM_EBADFD};
  }

  int wrk_index = vclWrkIndexOrRegister();
  RELEASE_ASSERT(wrk_index != -1, "should be initialized");

  vppcom_endpt_t endpt;
  vclEndptFromAddress(endpt, address);
  int32_t rv = vppcom_session_bind(sh_, &endpt);
  return {rv < 0 ? -1 : 0, -rv};
}

Api::SysCallIntResult VclIoHandle::listen(int) {
  int wrk_index = vclWrkIndexOrRegister();
  RELEASE_ASSERT(wrk_index != -1, "should be initialized");

  VCL_LOG("trying to listen sh {}", sh_);
  RELEASE_ASSERT(is_listener_ == false, "");
  RELEASE_ASSERT(vppcom_session_worker(sh_) == wrk_index, "");

  is_listener_ = true;

  if (!wrk_index) {
    not_listened_ = true;
  }

  return {0, 0};
}

Envoy::Network::IoHandlePtr VclIoHandle::accept(sockaddr* addr, socklen_t* addrlen) {
  int wrk_index = vclWrkIndexOrRegister();
  RELEASE_ASSERT(wrk_index != -1 && is_listener_, "must have worker and must be listener");

  uint32_t sh = sh_;
  if (wrk_index) {
    sh = wrk_listener_->sh();
    VCL_LOG("trying to accept fd {} sh {:x}", fd_, sh);
  }

  vppcom_endpt_t endpt;
  sockaddr_storage ss;
  endpt.ip = reinterpret_cast<uint8_t*>(&ss);
  int new_sh = vppcom_session_accept(sh, &endpt, O_NONBLOCK);
  if (new_sh >= 0) {
    vclEndptCopy(addr, addrlen, endpt);
    return std::make_unique<VclIoHandle>(new_sh, VclInvalidFd);
  }
  return nullptr;
}

Api::SysCallIntResult
VclIoHandle::connect(Envoy::Network::Address::InstanceConstSharedPtr address) {
  if (!VCL_SH_VALID(sh_)) {
    return {-1, VPPCOM_EBADFD};
  }
  vppcom_endpt_t endpt;
  uint8_t ipaddr[sizeof(absl::uint128)];
  endpt.ip = ipaddr;
  vclEndptFromAddress(endpt, address);
  int32_t rv = vppcom_session_connect(sh_, &endpt);
  return {rv < 0 ? -1 : 0, -rv};
}

Api::SysCallIntResult VclIoHandle::setOption(int level, int optname, const void* optval,
                                             socklen_t optlen) {
  if (!VCL_SH_VALID(sh_)) {
    return {-1, VPPCOM_EBADFD};
  }
  int32_t rv = 0;

  switch (level) {
  case SOL_TCP:
    switch (optname) {
    case TCP_NODELAY:
      rv =
          vppcom_session_attr(sh_, VPPCOM_ATTR_SET_TCP_NODELAY, const_cast<void*>(optval), &optlen);
      break;
    case TCP_MAXSEG:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_SET_TCP_USER_MSS, const_cast<void*>(optval),
                               &optlen);
      break;
    case TCP_KEEPIDLE:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_SET_TCP_KEEPIDLE, const_cast<void*>(optval),
                               &optlen);
      break;
    case TCP_KEEPINTVL:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_SET_TCP_KEEPINTVL, const_cast<void*>(optval),
                               &optlen);
      break;
    case TCP_CONGESTION:
    case TCP_CORK:
      /* Ignore */
      rv = 0;
      break;
    default:
      ENVOY_LOG(error, "setOption() SOL_TCP: sh {} optname {} unsupported!", sh_, optname);
      break;
    }
    break;
  case SOL_IPV6:
    switch (optname) {
    case IPV6_V6ONLY:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_SET_V6ONLY, const_cast<void*>(optval), &optlen);
      break;
    default:
      ENVOY_LOG(error, "setOption() SOL_IPV6: sh {} optname {} unsupported!", sh_, optname);
      break;
    }
    break;
  case SOL_SOCKET:
    switch (optname) {
    case SO_KEEPALIVE:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_SET_KEEPALIVE, const_cast<void*>(optval), &optlen);
      break;
    case SO_REUSEADDR:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_SET_REUSEADDR, const_cast<void*>(optval), &optlen);
      break;
    case SO_BROADCAST:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_SET_BROADCAST, const_cast<void*>(optval), &optlen);
      break;
    default:
      ENVOY_LOG(error, "setOption() SOL_SOCKET: sh {} optname {} unsupported!", sh_, optname);
      break;
    }
    break;
  default:
    break;
  }

  return {rv < 0 ? -1 : 0, -rv};
}

Api::SysCallIntResult VclIoHandle::getOption(int level, int optname, void* optval,
                                             socklen_t* optlen) {
  VCL_LOG("trying to get option");
  if (!VCL_SH_VALID(sh_)) {
    return {-1, VPPCOM_EBADFD};
  }
  int32_t rv = 0;

  switch (level) {
  case SOL_TCP:
    switch (optname) {
    case TCP_NODELAY:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_TCP_NODELAY, optval, optlen);
      break;
    case TCP_MAXSEG:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_TCP_USER_MSS, optval, optlen);
      break;
    case TCP_KEEPIDLE:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_TCP_KEEPIDLE, optval, optlen);
      break;
    case TCP_KEEPINTVL:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_TCP_KEEPINTVL, optval, optlen);
      break;
    case TCP_INFO:
      if (optval && optlen && (*optlen == sizeof(struct tcp_info))) {
        ENVOY_LOG(error, "getOption() TCP_INFO: sh %u optname %d unsupported!", sh_, optname);
        memset(optval, 0, *optlen);
        rv = VPPCOM_OK;
      } else {
        rv = -EFAULT;
      }
      break;
    case TCP_CONGESTION:
      *optlen = strlen("cubic");
      strncpy(static_cast<char*>(optval), "cubic", *optlen + 1);
      rv = 0;
      break;
    default:
      ENVOY_LOG(error, "getOption() SOL_TCP: sh %u optname %d unsupported!", sh_, optname);
      break;
    }
    break;
  case SOL_IPV6:
    switch (optname) {
    case IPV6_V6ONLY:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_V6ONLY, optval, optlen);
      break;
    default:
      ENVOY_LOG(error, "getOption() SOL_IPV6: sh %u optname %d unsupported!", sh_, optname);
      break;
    }
    break;
  case SOL_SOCKET:
    switch (optname) {
    case SO_ACCEPTCONN:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_LISTEN, optval, optlen);
      break;
    case SO_KEEPALIVE:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_KEEPALIVE, optval, optlen);
      break;
    case SO_PROTOCOL:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_PROTOCOL, optval, optlen);
      *static_cast<int*>(optval) = *static_cast<int*>(optval) ? SOCK_DGRAM : SOCK_STREAM;
      break;
    case SO_SNDBUF:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_TX_FIFO_LEN, optval, optlen);
      break;
    case SO_RCVBUF:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_RX_FIFO_LEN, optval, optlen);
      break;
    case SO_REUSEADDR:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_REUSEADDR, optval, optlen);
      break;
    case SO_BROADCAST:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_BROADCAST, optval, optlen);
      break;
    case SO_ERROR:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_ERROR, optval, optlen);
      break;
    default:
      ENVOY_LOG(error, "getOption() SOL_SOCKET: sh %u optname %d unsupported!", sh_, optname);
      ;
      break;
    }
    break;
  default:
    break;
  }
  return {rv < 0 ? -1 : 0, -rv};
}

Api::SysCallIntResult VclIoHandle::ioctl(unsigned long, void*, unsigned long, void*, unsigned long,
                                         unsigned long*) {
  return {0, 0};
}

Api::SysCallIntResult VclIoHandle::setBlocking(bool) {
  uint32_t flags = O_NONBLOCK;
  uint32_t buflen = sizeof(flags);
  int32_t rv = vppcom_session_attr(sh_, VPPCOM_ATTR_SET_FLAGS, &flags, &buflen);
  return {rv < 0 ? -1 : 0, -rv};
}

absl::optional<int> VclIoHandle::domain() {
  VCL_LOG("grabbing domain sh {:x}", sh_);
  return {AF_INET};
};

Envoy::Network::Address::InstanceConstSharedPtr VclIoHandle::localAddress() {
  vppcom_endpt_t ep;
  uint32_t eplen = sizeof(ep);
  uint8_t addr_buf[sizeof(struct sockaddr_in6)];
  ep.ip = addr_buf;
  if (vppcom_session_attr(sh_, VPPCOM_ATTR_GET_LCL_ADDR, &ep, &eplen)) {
    return nullptr;
  }
  return vclEndptToAddress(ep, sh_);
}

Envoy::Network::Address::InstanceConstSharedPtr VclIoHandle::peerAddress() {
  VCL_LOG("grabbing peer address sh {:x}", sh_);
  vppcom_endpt_t ep;
  uint32_t eplen = sizeof(ep);
  uint8_t addr_buf[sizeof(struct sockaddr_in6)];
  ep.ip = addr_buf;
  if (vppcom_session_attr(sh_, VPPCOM_ATTR_GET_PEER_ADDR, &ep, &eplen)) {
    return nullptr;
  }
  return vclEndptToAddress(ep, sh_);
}

void VclIoHandle::updateEvents(uint32_t events) {
  int wrk_index = vclWrkIndexOrRegister();
  VclIoHandle* vcl_handle = this;

  if (wrk_index && is_listener_) {
    vcl_handle = wrk_listener_.get();
  }

  struct epoll_event ev;
  ev.events = EPOLLET;

  if (events & Event::FileReadyType::Read) {
    ev.events |= EPOLLIN;
  }
  if (events & Event::FileReadyType::Write) {
    ev.events |= EPOLLOUT;
  }
  if (events & Event::FileReadyType::Closed) {
    ev.events |= EPOLLERR | EPOLLHUP;
  }

  ev.data.u64 = reinterpret_cast<uint64_t>(vcl_handle);

  vppcom_epoll_ctl(vclEpollHandle(wrk_index), EPOLL_CTL_MOD, vcl_handle->sh(), &ev);
  vclInterfaceDrainEvents();
}

void VclIoHandle::initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                                      Event::FileTriggerType, uint32_t events) {
  VCL_LOG("adding events for sh {:x} fd {} isListener {}", sh_, fd_, is_listener_);

  int wrk_index = vclWrkIndexOrRegister();
  vclInterfaceRegisterEpollEvent(dispatcher);

  VclIoHandle* vcl_handle = this;

  if (is_listener_) {
    if (wrk_index) {
      // If this is not the main worker, make sure a worker listener exists
      if (!wrk_listener_) {
        vppcom_endpt_t ep;
        uint8_t addr_buf[sizeof(struct sockaddr_in6)];
        ep.ip = addr_buf;
        uint32_t proto;

        RELEASE_ASSERT(peekVclSession(sh_, &ep, &proto) == false, "peek returned");

        Address::InstanceConstSharedPtr address = vclEndptToAddress(ep, -1);
        uint32_t sh = vppcom_session_create(proto, 1);
        wrk_listener_ = std::make_unique<VclIoHandle>(sh, VclInvalidFd);
        wrk_listener_->bind(address);
        uint32_t rv = vppcom_session_listen(sh, 0 /* ignored */);
        if (rv) {
          VCL_LOG("listen failed sh {:x}", sh);
          return;
        }
        wrk_listener_->setParentListener(this);
        VCL_LOG("add worker listener sh {:x} wrk_index {} new sh {:x}", sh_, wrk_index, sh);
      }
      vcl_handle = wrk_listener_.get();
    } else if (not_listened_) {
      // On main worker, no need to create worker listeners
      vppcom_session_listen(sh_, 0 /* ignored */);
      not_listened_ = false;
    }
  }

  struct epoll_event ev;
  ev.events = EPOLLET;

  if (events & Event::FileReadyType::Read) {
    ev.events |= EPOLLIN;
  }
  if (events & Event::FileReadyType::Write) {
    ev.events |= EPOLLOUT;
  }
  if (events & Event::FileReadyType::Closed) {
    ev.events |= EPOLLERR | EPOLLHUP;
  }

  cb_ = cb;
  ev.data.u64 = reinterpret_cast<uint64_t>(vcl_handle);
  vppcom_epoll_ctl(vclEpollHandle(wrk_index), EPOLL_CTL_ADD, vcl_handle->sh(), &ev);

  file_event_ = Event::FileEventPtr{new VclEvent(dispatcher, *vcl_handle, cb)};
  vclInterfaceDrainEvents();
}

void VclIoHandle::resetFileEvents() {
  if (!file_event_) {
    return;
  }
  // Remove session from epoll fd. This makes sure that when the even is recreated events already
  // consumed are regenerated.
  int wrk_index = vclWrkIndexOrRegister();
  if (VCL_SH_VALID(sh_) && wrk_index == vppcom_session_worker(sh_)) {
    vppcom_epoll_ctl(vclEpollHandle(wrk_index), EPOLL_CTL_DEL, sh_, nullptr);
  }
  file_event_.reset();
}

IoHandlePtr VclIoHandle::duplicate() {
  VCL_LOG("duplicate called");

  // Find what must be duplicated. Assume this is ONLY called for listeners
  vppcom_endpt_t ep;
  uint8_t addr_buf[sizeof(struct sockaddr_in6)];
  ep.ip = addr_buf;
  uint32_t proto;

  RELEASE_ASSERT(peekVclSession(sh_, &ep, &proto) == false, "peek returned");

  Address::InstanceConstSharedPtr address = vclEndptToAddress(ep, -1);
  uint32_t sh = vppcom_session_create(proto, 1);
  IoHandlePtr io_handle = std::make_unique<VclIoHandle>(sh, VclInvalidFd);

  io_handle->bind(address);

  return io_handle;
}

absl::optional<std::chrono::milliseconds> VclIoHandle::lastRoundTripTime() { return {}; }

absl::optional<uint64_t> VclIoHandle::congestionWindowInBytes() const { return {}; }

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy
