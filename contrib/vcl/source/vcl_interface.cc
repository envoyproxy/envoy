#include "contrib/vcl/source/vcl_interface.h"

#include "source/common/network/address_impl.h"

#include "contrib/vcl/source/vcl_io_handle.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace Vcl {

static uint32_t epoll_handles[128];
static std::mutex wrk_lock;
static absl::flat_hash_map<int, Envoy::Event::FileEventPtr> mq_events_;

uint32_t& vcl_epoll_handle(uint32_t wrk_index) { return epoll_handles[wrk_index]; }

static void onMqSocketEvents(uint32_t flags) {
  ASSERT((flags & (Event::FileReadyType::Read | Event::FileReadyType::Write)));
  auto wrk_index = vppcom_worker_index();
  VCL_LOG("events on worker %u", wrk_index);
  struct epoll_event events[128];
  int max_events = 128, n_events;

  while (max_events > 0) {
    n_events = vppcom_epoll_wait(epoll_handles[wrk_index], events, max_events, 0);
    if (n_events <= 0) {
      break;
    }
    max_events -= n_events;
    VCL_LOG("had %u events", n_events);

    for (int i = 0; i < n_events; i++) {
      VclIoHandle* vcl_handle = reinterpret_cast<VclIoHandle*>(events[i].data.u64);
      if (vcl_handle->isWrkListener()) {
        vcl_handle = vcl_handle->getParentListener();
      }

      // session closed due to some recently processed event
      if (!vcl_handle->isOpen())
        continue;

      uint32_t evts = 0;
      if (events[i].events & EPOLLIN) {
        evts |= Event::FileReadyType::Read;
      }
      if (events[i].events & EPOLLOUT) {
        evts |= Event::FileReadyType::Write;
      }
      if (events[i].events & (EPOLLERR | EPOLLHUP)) {
        RELEASE_ASSERT(vcl_handle->isOpen(),
                       fmt::format("handle 0x{:x} should be open", vcl_handle->sh()));
        evts |= Event::FileReadyType::Closed;
      }

      VCL_LOG("got event on vcl handle fd %u sh %x events %x", vcl_handle->fdDoNotUse(),
              vcl_handle->sh(), evts);
      vcl_handle->cb(evts);
    }
  }
}

void vcl_interface_worker_register() {
  wrk_lock.lock();
  vppcom_worker_register();
  wrk_lock.unlock();
  int epoll_handle = vppcom_epoll_create();
  if (epoll_handle < 0) {
    VCL_LOG("failed to create epoll handle");
    exit(1);
  }
  epoll_handles[vppcom_worker_index()] = epoll_handle;
  VCL_LOG("registered worker %u and epoll handle %u mq fd %d", vppcom_worker_index(), epoll_handle,
          vppcom_mq_epoll_fd());
}

void vcl_interface_register_epoll_event(Envoy::Event::Dispatcher& dispatcher) {
  if (mq_events_.find(vppcom_worker_index()) != mq_events_.end()) {
    return;
  }
  RELEASE_ASSERT(vppcom_worker_index() != -1, "");
  mq_events_[vppcom_worker_index()] = dispatcher.createFileEvent(
      vppcom_mq_epoll_fd(), [](uint32_t events) -> void { onMqSocketEvents(events); },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Write);
}

void vclSocketInterfaceInit(Event::Dispatcher& dispatcher) {
  vppcom_app_create("envoy");
  epoll_handles[vppcom_worker_index()] = vppcom_epoll_create();
  dispatcher.createFileEvent(
      vppcom_mq_epoll_fd(), [](uint32_t events) -> void { onMqSocketEvents(events); },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Write);
}

Envoy::Network::IoHandlePtr VclSocketInterface::socket(Envoy::Network::Socket::Type socket_type,
                                                       Envoy::Network::Address::Type addr_type,
                                                       Envoy::Network::Address::IpVersion,
                                                       bool) const {
  if (vppcom_worker_index() == -1) {
    vcl_interface_worker_register();
  }
  VCL_LOG("trying to create socket1 epoll fd %d", vppcom_mq_epoll_fd());
  if (addr_type == Envoy::Network::Address::Type::Pipe) {
    return nullptr;
  }
  auto sh = vppcom_session_create(
      socket_type == Envoy::Network::Socket::Type::Stream ? VPPCOM_PROTO_TCP : VPPCOM_PROTO_UDP, 1);
  if (sh < 0) {
    return nullptr;
  }
  return std::make_unique<VclIoHandle>(static_cast<uint32_t>(sh), 1 << 23);
}

Envoy::Network::IoHandlePtr
VclSocketInterface::socket(Envoy::Network::Socket::Type socket_type,
                           const Envoy::Network::Address::InstanceConstSharedPtr addr) const {
  if (vppcom_worker_index() == -1) {
    vcl_interface_worker_register();
  }
  VCL_LOG("trying to create socket2 epoll fd %d", vppcom_mq_epoll_fd());
  if (addr->type() == Envoy::Network::Address::Type::Pipe) {
    return nullptr;
  }
  auto sh = vppcom_session_create(
      socket_type == Envoy::Network::Socket::Type::Stream ? VPPCOM_PROTO_TCP : VPPCOM_PROTO_UDP, 1);
  if (sh < 0) {
    return nullptr;
  }
  return std::make_unique<VclIoHandle>(static_cast<uint32_t>(sh), 1 << 23);
}

bool VclSocketInterface::ipFamilySupported(int) { return true; };

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy
