#include "contrib/vcl/source/vcl_interface.h"

#include "source/common/network/address_impl.h"

#include "contrib/vcl/source/vcl_io_handle.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace Vcl {

namespace {

constexpr int MaxNumWorkers = 128;
constexpr int MaxNumEpollEvents = 128;

uint32_t epoll_handles[MaxNumWorkers];
std::mutex wrk_lock;

using MqFileEventsMap = absl::flat_hash_map<int, Envoy::Event::FileEventPtr>;

static MqFileEventsMap& mqFileEventsMap() { MUTABLE_CONSTRUCT_ON_FIRST_USE(MqFileEventsMap); }

static void onMqSocketEvents(uint32_t flags) {
  ASSERT((flags & (Event::FileReadyType::Read | Event::FileReadyType::Write)));
  auto wrk_index = vppcom_worker_index();
  VCL_LOG("events on worker {}", wrk_index);
  struct epoll_event events[MaxNumEpollEvents];
  int max_events = MaxNumEpollEvents, n_events;

  while (max_events > 0) {
    n_events = vppcom_epoll_wait(epoll_handles[wrk_index], events, max_events, 0);
    if (n_events <= 0) {
      break;
    }
    max_events -= n_events;
    VCL_LOG("had {} events", n_events);

    for (int i = 0; i < n_events; i++) {
      VclIoHandle* vcl_handle = reinterpret_cast<VclIoHandle*>(events[i].data.u64);
      if (vcl_handle->isWrkListener()) {
        vcl_handle = vcl_handle->getParentListener();
      }

      // session closed due to some recently processed event
      if (!vcl_handle->isOpen()) {
        continue;
      }

      uint32_t evts = 0;
      if (events[i].events & EPOLLIN) {
        evts |= Event::FileReadyType::Read;
      }
      if (events[i].events & EPOLLOUT) {
        evts |= Event::FileReadyType::Write;
      }
      if (events[i].events & (EPOLLERR | EPOLLHUP)) {
        evts |= Event::FileReadyType::Closed;
      }

      VCL_LOG("got event on vcl handle fd {} sh {:x} events {}", vcl_handle->fdDoNotUse(),
              vcl_handle->sh(), evts);
      vcl_handle->cb(evts);
    }
  }
}

} // namespace

uint32_t& vclEpollHandle(uint32_t wrk_index) { return epoll_handles[wrk_index]; }

void vclInterfaceWorkerRegister() {
  std::lock_guard<std::mutex> lk(wrk_lock);
  vppcom_worker_register();
  int epoll_handle = vppcom_epoll_create();
  if (epoll_handle < 0) {
    RELEASE_ASSERT(0, "failed to create epoll handle");
  }
  epoll_handles[vppcom_worker_index()] = epoll_handle;
  VCL_LOG("registered worker {} and epoll handle {} mq fd {}", vppcom_worker_index(), epoll_handle,
          vppcom_mq_epoll_fd());
}

void vclInterfaceRegisterEpollEvent(Envoy::Event::Dispatcher& dispatcher) {
  MqFileEventsMap& mq_fevts_map = mqFileEventsMap();
  if (mq_fevts_map.find(vppcom_worker_index()) != mq_fevts_map.end()) {
    return;
  }
  RELEASE_ASSERT(vppcom_worker_index() != -1, "");
  mq_fevts_map[vppcom_worker_index()] = dispatcher.createFileEvent(
      vppcom_mq_epoll_fd(), [](uint32_t events) -> void { onMqSocketEvents(events); },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Write);
}

void vclInterfaceInit(Event::Dispatcher& dispatcher) {
  MqFileEventsMap& mq_fevts_map = mqFileEventsMap();
  vppcom_app_create("envoy");
  epoll_handles[vppcom_worker_index()] = vppcom_epoll_create();
  mq_fevts_map[vppcom_worker_index()] = dispatcher.createFileEvent(
      vppcom_mq_epoll_fd(), [](uint32_t events) -> void { onMqSocketEvents(events); },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Write);
}

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy
