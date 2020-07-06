#include "common/network/udp_listener_impl.h"

#include <cerrno>
#include <csetjmp>
#include <cstring>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/exception.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/address_impl.h"
#include "common/network/io_socket_error_impl.h"

#include "absl/container/fixed_array.h"
#include "event2/listener.h"

#define ENVOY_UDP_LOG(LEVEL, FORMAT, ...)                                                          \
  ENVOY_LOG_TO_LOGGER(ENVOY_LOGGER(), LEVEL, "Listener at {} :" FORMAT,                            \
                      this->localAddress()->asString(), ##__VA_ARGS__)

namespace Envoy {
namespace Network {

UdpListenerImpl::UdpListenerImpl(Event::DispatcherImpl& dispatcher, SocketSharedPtr socket,
                                 UdpListenerCallbacks& cb, TimeSource& time_source)
    : BaseListenerImpl(dispatcher, std::move(socket)), cb_(cb), time_source_(time_source) {
  file_event_ = dispatcher_.createFileEvent(
      socket_->ioHandle().fd(), [this](uint32_t events) -> void { onSocketEvent(events); },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Write);

  ASSERT(file_event_);

  if (!Network::Socket::applyOptions(socket_->options(), *socket_,
                                     envoy::config::core::v3::SocketOption::STATE_BOUND)) {
    throw CreateListenerException(fmt::format("cannot set post-bound socket option on socket: {}",
                                              socket_->localAddress()->asString()));
  }
}

UdpListenerImpl::~UdpListenerImpl() {
  disable();
  file_event_.reset();
}

void UdpListenerImpl::disable() { file_event_->setEnabled(0); }

void UdpListenerImpl::enable() {
  file_event_->setEnabled(Event::FileReadyType::Read | Event::FileReadyType::Write);
}

void UdpListenerImpl::onSocketEvent(short flags) {
  ASSERT((flags & (Event::FileReadyType::Read | Event::FileReadyType::Write)));
  ENVOY_UDP_LOG(trace, "socket event: {}", flags);

  if (flags & Event::FileReadyType::Read) {
    handleReadCallback();
  }

  if (flags & Event::FileReadyType::Write) {
    handleWriteCallback();
  }
}

void UdpListenerImpl::handleReadCallback() {
  ENVOY_UDP_LOG(trace, "handleReadCallback");
  cb_.onReadReady();
  const Api::IoErrorPtr result = Utility::readPacketsFromSocket(
      socket_->ioHandle(), *socket_->localAddress(), *this, time_source_, packets_dropped_);
  // TODO(mattklein123): Handle no error when we limit the number of packets read.
  if (result->getErrorCode() != Api::IoError::IoErrorCode::Again) {
    // TODO(mattklein123): When rate limited logging is implemented log this at error level
    // on a periodic basis.
    ENVOY_UDP_LOG(debug, "recvmsg result {}: {}", static_cast<int>(result->getErrorCode()),
                  result->getErrorDetails());
    cb_.onReceiveError(result->getErrorCode());
  }
}

void UdpListenerImpl::processPacket(Address::InstanceConstSharedPtr local_address,
                                    Address::InstanceConstSharedPtr peer_address,
                                    Buffer::InstancePtr buffer, MonotonicTime receive_time) {
  // UDP listeners are always configured with the socket option that allows pulling the local
  // address. This should never be null.
  ASSERT(local_address != nullptr);
  UdpRecvData recvData{
      {std::move(local_address), std::move(peer_address)}, std::move(buffer), receive_time};
  cb_.onData(recvData);
}

void UdpListenerImpl::handleWriteCallback() {
  ENVOY_UDP_LOG(trace, "handleWriteCallback");
  cb_.onWriteReady(*socket_);
}

Event::Dispatcher& UdpListenerImpl::dispatcher() { return dispatcher_; }

const Address::InstanceConstSharedPtr& UdpListenerImpl::localAddress() const {
  return socket_->localAddress();
}

Api::IoCallUint64Result UdpListenerImpl::send(const UdpSendData& send_data) {
  ENVOY_UDP_LOG(trace, "send");
  Buffer::Instance& buffer = send_data.buffer_;
  Api::IoCallUint64Result send_result = Utility::writeToSocket(
      socket_->ioHandle(), buffer, send_data.local_ip_, send_data.peer_address_);

  // The send_result normalizes the rc_ value to 0 in error conditions.
  // The drain call is hence 'safe' in success and failure cases.
  buffer.drain(send_result.rc_);
  return send_result;
}

} // namespace Network
} // namespace Envoy
