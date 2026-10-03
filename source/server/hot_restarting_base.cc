#include "source/server/hot_restarting_base.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/mem_block_builder.h"
#include "source/common/common/safe_memcpy.h"
#include "source/common/common/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/stats/utility.h"

namespace Envoy {
namespace Server {

using HotRestartMessage = envoy::HotRestartMessage;

static constexpr uint64_t MaxSendmsgSize = 4096;
static constexpr absl::Duration CONNECTION_REFUSED_RETRY_DELAY = absl::Seconds(1);
static constexpr int SENDMSG_MAX_RETRIES = 10;

RpcStream::~RpcStream() {
  if (domain_socket_ != -1) {
    Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
    Api::SysCallIntResult result = os_sys_calls.close(domain_socket_);
    ASSERT(result.return_value_ == 0);
  }
}

void RpcStream::initDomainSocketAddress(sockaddr_un* address) {
  memset(address, 0, sizeof(*address));
  address->sun_family = AF_UNIX;
}

sockaddr_un RpcStream::createDomainSocketAddress(uint64_t id, const std::string& role,
                                                 const std::string& socket_path,
                                                 mode_t socket_mode) {
  // Right now we only allow a maximum of 3 concurrent envoy processes to be running. When the third
  // starts up it will kill the oldest parent.
  static constexpr uint64_t MaxConcurrentProcesses = 3;
  id = id % MaxConcurrentProcesses;
  sockaddr_un address;
  initDomainSocketAddress(&address);
  auto addr = THROW_OR_RETURN_VALUE(
      Network::Address::PipeInstance::create(
          fmt::format("{}_{}_{}", socket_path, role, base_id_ + id), socket_mode, nullptr),
      std::unique_ptr<Network::Address::PipeInstance>);
  safeMemcpy(&address, &(addr->getSockAddr()));
  fchmod(domain_socket_, socket_mode);

  return address;
}

void RpcStream::bindDomainSocket(uint64_t id, const std::string& role,
                                 const std::string& socket_path, mode_t socket_mode) {
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  // This actually creates the socket and binds it. We use the socket in datagram mode so we can
  // easily read single messages.
  domain_socket_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  sockaddr_un address = createDomainSocketAddress(id, role, socket_path, socket_mode);
  unlink(address.sun_path);
  Api::SysCallIntResult result =
      os_sys_calls.bind(domain_socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  if (result.return_value_ != 0) {
    const auto msg = fmt::format(
        "unable to bind domain socket with base_id={}, id={}, errno={} (see --base-id option)",
        base_id_, id, result.errno_);
    if (result.errno_ == SOCKET_ERROR_ADDR_IN_USE) {
      throw HotRestartDomainSocketInUseException(msg);
    }
    throw EnvoyException(msg);
  }
  setSocketTimeouts();
}

void RpcStream::serializeToDatagrams(const HotRestartMessage& proto,
                                     std::vector<std::vector<uint8_t>>& datagrams) {
  const uint64_t serialized_size = proto.ByteSizeLong();
  const uint64_t total_size = sizeof(uint64_t) + serialized_size;
  // Fill with uint64_t 'length' followed by the serialized HotRestartMessage.
  std::vector<uint8_t> send_buf;
  send_buf.resize(total_size);
  *reinterpret_cast<uint64_t*>(send_buf.data()) = htobe64(serialized_size);
  RELEASE_ASSERT(proto.SerializeWithCachedSizesToArray(send_buf.data() + sizeof(uint64_t)),
                 "failed to serialize a HotRestartMessage");
  for (uint64_t offset = 0; offset < total_size; offset += MaxSendmsgSize) {
    const uint64_t cur_chunk_size = std::min(MaxSendmsgSize, total_size - offset);
    datagrams.emplace_back(send_buf.begin() + offset, send_buf.begin() + offset + cur_chunk_size);
  }
}

void RpcStream::setSocketTimeouts() {
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  const auto to_timeval = [](std::chrono::milliseconds duration) {
    timeval tv;
    tv.tv_sec = duration.count() / 1000;
    tv.tv_usec = (duration.count() % 1000) * 1000;
    return tv;
  };
  const timeval send_timeout = to_timeval(SEND_TIMEOUT);
  Api::SysCallIntResult result = os_sys_calls.setsockopt(domain_socket_, SOL_SOCKET, SO_SNDTIMEO,
                                                         &send_timeout, sizeof(send_timeout));
  RELEASE_ASSERT(result.return_value_ == 0,
                 fmt::format("Set domain socket send timeout failed, errno = {}", result.errno_));
  const timeval receive_timeout = to_timeval(RECEIVE_TIMEOUT_SLICE);
  result = os_sys_calls.setsockopt(domain_socket_, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout,
                                   sizeof(receive_timeout));
  RELEASE_ASSERT(result.return_value_ == 0, fmt::format("Set domain socket receive timeout "
                                                        "failed, errno = {}",
                                                        result.errno_));
}

bool RpcStream::sendHotRestartMessage(sockaddr_un& address, const HotRestartMessage& proto,
                                      bool allow_failure) {
  return sendHotRestartMessageImpl(address, proto, allow_failure) == SendResult::Sent;
}

RpcStream::SendResult RpcStream::trySendHotRestartMessage(sockaddr_un& address,
                                                          const HotRestartMessage& proto) {
  return sendHotRestartMessageImpl(address, proto, /*allow_failure=*/true);
}

RpcStream::SendResult RpcStream::sendHotRestartMessageImpl(sockaddr_un& address,
                                                           const HotRestartMessage& proto,
                                                           bool allow_failure) {
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  std::vector<std::vector<uint8_t>> datagrams;
  serializeToDatagrams(proto, datagrams);

  RELEASE_ASSERT(fcntl(domain_socket_, F_SETFL, 0) != -1,
                 fmt::format("Set domain socket blocking failed, errno = {}", errno));
  const auto restore_nonblocking = [this]() {
    RELEASE_ASSERT(fcntl(domain_socket_, F_SETFL, O_NONBLOCK) != -1,
                   fmt::format("Set domain socket nonblocking failed, errno = {}", errno));
  };

  const bool passes_fd = replyIsExpectedType(&proto, HotRestartMessage::Reply::kPassListenSocket) &&
                         proto.reply().pass_listen_socket().fd() != -1;
  for (uint64_t i = 0; i < datagrams.size(); i++) {
    std::vector<uint8_t>& datagram = datagrams[i];
    iovec iov[1];
    iov[0].iov_base = datagram.data();
    iov[0].iov_len = datagram.size();
    msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_name = &address;
    message.msg_namelen = sizeof(address);
    message.msg_iov = iov;
    message.msg_iovlen = 1;

    // Control data stuff, only relevant for the fd passing done with PassListenSocketReply.
    uint8_t control_buffer[CMSG_SPACE(sizeof(int))];
    if (passes_fd) {
      memset(control_buffer, 0, CMSG_SPACE(sizeof(int)));
      message.msg_control = control_buffer;
      message.msg_controllen = CMSG_SPACE(sizeof(int));
      cmsghdr* control_message = CMSG_FIRSTHDR(&message);
      control_message->cmsg_level = SOL_SOCKET;
      control_message->cmsg_type = SCM_RIGHTS;
      control_message->cmsg_len = CMSG_LEN(sizeof(int));
      *reinterpret_cast<int*>(CMSG_DATA(control_message)) = proto.reply().pass_listen_socket().fd();
      ASSERT(datagrams.size() == 1, "an fd passing message was too long for one sendmsg().");
    }

    // A transient connection refused error probably means the old process is not ready.
    int saved_errno = 0;
    int rc = 0;
    bool sent = false;
    for (int attempt = 0; attempt < SENDMSG_MAX_RETRIES; attempt++) {
      auto result = os_sys_calls.sendmsg(domain_socket_, &message, 0);
      rc = result.return_value_;
      saved_errno = result.errno_;

      if (rc == static_cast<int>(datagram.size())) {
        sent = true;
        break;
      }

      if (saved_errno == ECONNREFUSED) {
        if (allow_failure) {
          restore_nonblocking();
          return SendResult::ConnectionRefused;
        }
        ENVOY_LOG(error, "hot restart sendmsg() connection refused, retrying");
        absl::SleepFor(CONNECTION_REFUSED_RETRY_DELAY);
        continue;
      }

      if (saved_errno == SOCKET_ERROR_AGAIN) {
        // The peer is not draining its socket (SEND_TIMEOUT elapsed). Blocking on it any longer
        // would stall this thread on the other process: give up on the message instead.
        ENVOY_LOG(error,
                  "hot restart sendmsg() timed out after {}ms (datagram {} of {}); giving up on "
                  "the message",
                  SEND_TIMEOUT.count(), i + 1, datagrams.size());
        restore_nonblocking();
        return SendResult::TimedOut;
      }

      RELEASE_ASSERT(false, fmt::format("hot restart sendmsg() failed: returned {}, errno {}", rc,
                                        saved_errno));
    }

    if (!sent) {
      RELEASE_ASSERT(false, fmt::format("hot restart sendmsg() failed: returned {}, errno {}", rc,
                                        saved_errno));
    }
  }

  restore_nonblocking();
  return SendResult::Sent;
}

bool RpcStream::queueHotRestartMessage(const sockaddr_un& address, const HotRestartMessage& proto) {
  std::vector<std::vector<uint8_t>> datagrams;
  serializeToDatagrams(proto, datagrams);
  uint64_t message_bytes = 0;
  for (const auto& datagram : datagrams) {
    message_bytes += datagram.size();
  }
  if (send_queue_bytes_ + message_bytes > MAX_SEND_QUEUE_BYTES) {
    return false;
  }
  send_queue_address_ = address;
  for (auto& datagram : datagrams) {
    send_queue_.push_back(std::move(datagram));
  }
  send_queue_bytes_ += message_bytes;
  return true;
}

bool RpcStream::flushSendQueue() {
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  last_flush_dropped_ = 0;
  while (!send_queue_.empty()) {
    std::vector<uint8_t>& datagram = send_queue_.front();
    iovec iov[1];
    iov[0].iov_base = datagram.data();
    iov[0].iov_len = datagram.size();
    msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_name = &send_queue_address_;
    message.msg_namelen = sizeof(send_queue_address_);
    message.msg_iov = iov;
    message.msg_iovlen = 1;

    // The socket is non-blocking (it is only ever made blocking for the duration of a
    // sendHotRestartMessage() or blocking receiveHotRestartMessage() call on this thread), and
    // the don't-wait flag on the call makes sure of it.
    const Api::SysCallSizeResult result =
        os_sys_calls.sendmsg(domain_socket_, &message, MSG_DONTWAIT);
    if (result.return_value_ == static_cast<ssize_t>(datagram.size())) {
      send_queue_bytes_ -= datagram.size();
      send_queue_.pop_front();
      continue;
    }
    if (result.return_value_ == -1 &&
        (result.errno_ == SOCKET_ERROR_AGAIN || result.errno_ == ENOBUFS ||
         result.errno_ == ENOMEM || result.errno_ == EINTR)) {
      // The peer's socket queue (or our send buffer) is full; leave the rest for the next flush.
      return false;
    }
    ENVOY_LOG_PERIODIC(error, std::chrono::seconds(5),
                       "hot restart non-blocking sendmsg() failed: returned {}, errno {}; dropping "
                       "{} queued datagrams ({} bytes)",
                       result.return_value_, result.errno_, send_queue_.size(), send_queue_bytes_);
    last_flush_dropped_ = send_queue_.size();
    send_queue_.clear();
    send_queue_bytes_ = 0;
    return true;
  }
  return true;
}

bool RpcStream::replyIsExpectedType(const HotRestartMessage* proto,
                                    HotRestartMessage::Reply::ReplyCase oneof_type) const {
  return proto != nullptr && proto->requestreply_case() == HotRestartMessage::kReply &&
         proto->reply().reply_case() == oneof_type;
}

// Pull the cloned fd, if present, out of the control data and write it into the
// PassListenSocketReply proto; the higher level code will see a listening fd that Just Works. We
// should only get control data in a PassListenSocketReply, it should only be the fd passing type,
// and there should only be one at a time. Crash on any other control data.
void RpcStream::getPassedFdIfPresent(HotRestartMessage* out, msghdr* message) {
  // NOLINTNEXTLINE(clang-analyzer-core.UndefinedBinaryOperatorResult)
  cmsghdr* cmsg = CMSG_FIRSTHDR(message);
  if (cmsg != nullptr) {
    RELEASE_ASSERT(cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
                       replyIsExpectedType(out, HotRestartMessage::Reply::kPassListenSocket),
                   "recvmsg() came with control data when the message's purpose was not to pass a "
                   "file descriptor.");

    out->mutable_reply()->mutable_pass_listen_socket()->set_fd(
        *reinterpret_cast<int*>(CMSG_DATA(cmsg)));

    RELEASE_ASSERT(CMSG_NXTHDR(message, cmsg) == nullptr,
                   "More than one control data on a single hot restart recvmsg().");
  }
}

// While in use, recv_buf_ is always >= MaxSendmsgSize. In between messages, it is kept empty,
// to be grown back to MaxSendmsgSize at the start of the next message.
void RpcStream::initRecvBufIfNewMessage() {
  if (recv_buf_.empty()) {
    ASSERT(cur_msg_recvd_bytes_ == 0);
    ASSERT(!expected_proto_length_.has_value());
    recv_buf_.resize(MaxSendmsgSize);
  }
}

// Must only be called when recv_buf_ contains a full proto. Returns that proto, and resets all of
// our receive-buffering state back to empty, to await a new message.
std::unique_ptr<HotRestartMessage> RpcStream::parseProtoAndResetState() {
  auto ret = std::make_unique<HotRestartMessage>();
  RELEASE_ASSERT(
      ret->ParseFromArray(recv_buf_.data() + sizeof(uint64_t), expected_proto_length_.value()),
      "failed to parse a HotRestartMessage.");
  recv_buf_.resize(0);
  cur_msg_recvd_bytes_ = 0;
  expected_proto_length_.reset();
  return ret;
}

std::unique_ptr<HotRestartMessage> RpcStream::receiveHotRestartMessage(Blocking block) {
  // By default the domain socket is non blocking. If we need to block, make it blocking first.
  if (block == Blocking::Yes) {
    RELEASE_ASSERT(fcntl(domain_socket_, F_SETFL, 0) != -1,
                   fmt::format("Set domain socket blocking failed, errno = {}", errno));
  }

  initRecvBufIfNewMessage();

  iovec iov[1];
  msghdr message;
  uint8_t control_buffer[CMSG_SPACE(sizeof(int))];
  std::unique_ptr<HotRestartMessage> ret = nullptr;
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  while (!ret) {
    iov[0].iov_base = recv_buf_.data() + cur_msg_recvd_bytes_;
    iov[0].iov_len = MaxSendmsgSize;

    // We always setup to receive an FD even though most messages do not pass one.
    memset(control_buffer, 0, CMSG_SPACE(sizeof(int)));
    memset(&message, 0, sizeof(message));
    message.msg_iov = iov;
    message.msg_iovlen = 1;
    message.msg_control = control_buffer;
    message.msg_controllen = CMSG_SPACE(sizeof(int));

    const Api::SysCallSizeResult recv_result = os_sys_calls.recvmsg(domain_socket_, &message, 0);
    if (recv_result.return_value_ == -1 && recv_result.errno_ == SOCKET_ERROR_AGAIN) {
      // Non-blocking: out of data. Blocking: RECEIVE_TIMEOUT_SLICE elapsed without data. Either
      // way, hand control back to the caller; any partial message stays buffered for the next
      // call.
      if (block == Blocking::Yes) {
        RELEASE_ASSERT(fcntl(domain_socket_, F_SETFL, O_NONBLOCK) != -1,
                       fmt::format("Set domain socket nonblocking failed, errno = {}", errno));
      }
      return nullptr;
    }
    RELEASE_ASSERT(recv_result.return_value_ != -1,
                   fmt::format("recvmsg() returned -1, errno = {}", recv_result.errno_));
    RELEASE_ASSERT(message.msg_flags == 0,
                   fmt::format("recvmsg() left msg_flags = {}", message.msg_flags));
    cur_msg_recvd_bytes_ += recv_result.return_value_;

    // If we don't already know 'length', we're at the start of a new length+protobuf message!
    if (!expected_proto_length_.has_value()) {
      // We are not ok with messages so fragmented that the length doesn't even come in one piece.
      RELEASE_ASSERT(recv_result.return_value_ >= 8, "received a brokenly tiny message fragment.");

      expected_proto_length_ = be64toh(*reinterpret_cast<uint64_t*>(recv_buf_.data()));
      // Expand the buffer from its default 4096 if this message is going to be longer.
      if (expected_proto_length_.value() > MaxSendmsgSize - sizeof(uint64_t)) {
        recv_buf_.resize(expected_proto_length_.value() + sizeof(uint64_t));
        cur_msg_recvd_bytes_ = recv_result.return_value_;
      }
    }
    // If we have received beyond the end of the current in-flight proto, then next is misaligned.
    RELEASE_ASSERT(cur_msg_recvd_bytes_ <= sizeof(uint64_t) + expected_proto_length_.value(),
                   "received a length+protobuf message not aligned to start of sendmsg().");

    if (cur_msg_recvd_bytes_ == sizeof(uint64_t) + expected_proto_length_.value()) {
      ret = parseProtoAndResetState();
    }
  }

  // Turn non-blocking back on if we made it blocking.
  if (block == Blocking::Yes) {
    RELEASE_ASSERT(fcntl(domain_socket_, F_SETFL, O_NONBLOCK) != -1,
                   fmt::format("Set domain socket nonblocking failed, errno = {}", errno));
  }
  getPassedFdIfPresent(ret.get(), &message);
  return ret;
}

Stats::Gauge& HotRestartingBase::hotRestartGeneration(Stats::Scope& scope) {
  // Track the hot-restart generation. Using gauge's accumulate semantics,
  // the increments will be combined across hot-restart. This may be useful
  // at some point, though the main motivation for this stat is to enable
  // an integration test showing that dynamic stat-names can be coalesced
  // across hot-restarts. There's no other reason this particular stat-name
  // needs to be created dynamically.
  //
  // Note also, this stat cannot currently be represented as a counter due to
  // the way stats get latched on sink update. See the comment in
  // InstanceUtil::flushMetricsToSinks.
  return Stats::Utility::gaugeFromElements(scope,
                                           {Stats::DynamicName("server.hot_restart_generation")},
                                           Stats::Gauge::ImportMode::Accumulate);
}

} // namespace Server
} // namespace Envoy
