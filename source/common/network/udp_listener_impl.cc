#include "source/common/network/udp_listener_impl.h"

#include <cerrno>
#include <csetjmp>
#include <cstring>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/exception.h"
#include "envoy/network/parent_drained_callback_registrar.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_error_impl.h"

#include "absl/container/fixed_array.h"
#include "event2/listener.h"

#define ENVOY_UDP_LOG(LEVEL, FORMAT, ...)                                                          \
  ENVOY_LOG_TO_LOGGER(ENVOY_LOGGER(), LEVEL, "Listener at {} :" FORMAT,                            \
                      this->localAddress()->asString(), ##__VA_ARGS__)

namespace Envoy {
namespace Network {

UdpListenerImpl::UdpListenerImpl(Event::Dispatcher& dispatcher, SocketSharedPtr socket,
                                 UdpListenerCallbacks& cb, TimeSource& time_source,
                                 const envoy::config::core::v3::UdpSocketConfig& config)
    : BaseListenerImpl(dispatcher, std::move(socket)), cb_(cb), time_source_(time_source),
      // Default prefer_gro to false for downstream server traffic.
      config_(config, false) {
  parent_drained_callback_registrar_ = socket_->parentDrainedCallbackRegistrar();
  socket_->ioHandle().initializeFileEvent(
      dispatcher,
      [this](uint32_t events) {
        onSocketEvent(events);
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, paused() ? 0 : events_when_unpaused_);
  if (paused()) {
    parent_drained_callback_registrar_->registerParentDrainedCallback(
        socket_->connectionInfoProvider().localAddress(),
        [this, &dispatcher, alive = std::weak_ptr<void>(destruction_checker_)]() {
          dispatcher.post([this, alive = std::move(alive)]() {
            auto still_alive = alive.lock();
            if (still_alive != nullptr) {
              unpause();
            }
          });
        });
  }
}

void UdpListenerImpl::unpause() {
  // Remove the paused state so enable will actually start listening to events.
  parent_drained_callback_registrar_ = absl::nullopt;
  if (events_when_unpaused_ != 0) {
    // Start listening to events.
    enable();
    // There may have already been events while this instance was ignoring them,
    // so try reading immediately.
    activateRead();
  }
}

UdpListenerImpl::~UdpListenerImpl() { socket_->ioHandle().resetFileEvents(); }

void UdpListenerImpl::disable() { disableEvent(); }

void UdpListenerImpl::enable() {
  events_when_unpaused_ = Event::FileReadyType::Read | Event::FileReadyType::Write;
  if (!paused()) {
    socket_->ioHandle().enableFileEvents(events_when_unpaused_);
  }
}

void UdpListenerImpl::disableEvent() {
  events_when_unpaused_ = 0;
  if (!paused()) {
    socket_->ioHandle().enableFileEvents(0);
  }
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
      socket_->ioHandle(), *socket_->connectionInfoProvider().localAddress(), *this, time_source_,
      config_.prefer_gro_, /*allow_mmsg=*/true, packets_dropped_);
  if (result == nullptr) {
    // No error. The number of reads was limited by read rate. There are more packets to read.
    // Register to read more in the next event loop.
    socket_->ioHandle().activateFileEvents(Event::FileReadyType::Read);
    return;
  }
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
                                    Buffer::InstancePtr buffer, MonotonicTime receive_time,
                                    uint8_t tos, Buffer::RawSlice saved_cmsg) {
  // UDP listeners are always configured with the socket option that allows pulling the local
  // address. This should never be null.
  ASSERT(local_address != nullptr);
  UdpRecvData recvData{{std::move(local_address), std::move(peer_address)},
                       std::move(buffer),
                       receive_time,
                       tos,
                       saved_cmsg};
  cb_.onData(std::move(recvData));
}

void UdpListenerImpl::handleWriteCallback() {
  ENVOY_UDP_LOG(trace, "handleWriteCallback");
  cb_.onWriteReady(*socket_);
}

Event::Dispatcher& UdpListenerImpl::dispatcher() { return dispatcher_; }

const Address::InstanceConstSharedPtr& UdpListenerImpl::localAddress() const {
  return socket_->connectionInfoProvider().localAddress();
}

Api::IoCallUint64Result UdpListenerImpl::send(const UdpSendData& send_data) {
  ENVOY_UDP_LOG(trace, "send");
  Buffer::Instance& buffer = send_data.buffer_;

  Api::IoCallUint64Result send_result =
      cb_.udpPacketWriter().writePacket(buffer, send_data.local_ip_, send_data.peer_address_);

  // The send_result normalizes the return_value_ value to 0 in error conditions.
  // The drain call is hence 'safe' in success and failure cases.
  buffer.drain(send_result.return_value_);
  return send_result;
}

Api::IoCallUint64Result UdpListenerImpl::flush() {
  ENVOY_UDP_LOG(trace, "flush");
  return cb_.udpPacketWriter().flush();
}

void UdpListenerImpl::activateRead() {
  socket_->ioHandle().activateFileEvents(Event::FileReadyType::Read);
}

UdpListenerWorkerRouterImpl::UdpListenerWorkerRouterImpl(uint32_t concurrency)
    : workers_(concurrency) {}

void UdpListenerWorkerRouterImpl::registerWorkerForListener(UdpListenerCallbacks& listener) {
  absl::WriterMutexLock lock(&mutex_);

  ASSERT(listener.workerIndex() < workers_.size());
  ASSERT(workers_.at(listener.workerIndex()) == nullptr);
  workers_.at(listener.workerIndex()) = &listener;
}

void UdpListenerWorkerRouterImpl::unregisterWorkerForListener(UdpListenerCallbacks& listener) {
  absl::WriterMutexLock lock(&mutex_);

  ASSERT(workers_.at(listener.workerIndex()) == &listener);
  workers_.at(listener.workerIndex()) = nullptr;
}

void UdpListenerWorkerRouterImpl::deliver(uint32_t dest_worker_index, UdpRecvData&& data) {
  absl::ReaderMutexLock lock(&mutex_);

  ASSERT(dest_worker_index < workers_.size(),
         "UdpListenerCallbacks::destination returned out-of-range value");
  auto* worker = workers_[dest_worker_index];

  // When a listener is being removed, packets could be processed on some workers after the
  // listener is removed from other workers, which could result in a nullptr for that worker.
  if (worker != nullptr) {
    worker->post(std::move(data));
  }
}

} // namespace Network
} // namespace Envoy
