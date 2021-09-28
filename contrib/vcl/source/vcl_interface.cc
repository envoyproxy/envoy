#include "contrib/vcl/source/vcl_interface.h"

#include "source/common/network/address_impl.h"

#include "contrib/vcl/source/vcl_io_handle.h"
#include "vppcom.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace Vcl {

namespace {

/**
 * Max number of workers supported. Used in declaration of epoll_handles array
 */
const int MaxNumWorkers = 128;

/**
 * Max number of epoll events to drain from VCL per `vppcom_epoll_wait` call
 */
const int MaxNumEpollEvents = 128;

/**
 * Envoy worker epoll session handles by VCL worker index, i.e., `vppcom_worker_index()`. Each
 * worker uses its respective handle to retrieve session events from VCL via `vppcom_epoll_wait()`.
 */
uint32_t epoll_handles[MaxNumWorkers];

/**
 * Mutex only used during VCL worker registration
 */
ABSL_CONST_INIT absl::Mutex wrk_lock(absl::kConstInit);

using MqFileEventsMap = absl::flat_hash_map<int, Envoy::Event::FileEventPtr>;

MqFileEventsMap& mqFileEventsMap() { MUTABLE_CONSTRUCT_ON_FIRST_USE(MqFileEventsMap); }

void onMqSocketEvents(uint32_t flags) {
  ASSERT((flags & (Event::FileReadyType::Read | Event::FileReadyType::Write)));
  int wrk_index = vppcom_worker_index();
  VCL_LOG("events on worker {}", wrk_index);
  struct epoll_event events[MaxNumEpollEvents];
  int max_events = MaxNumEpollEvents;

  while (max_events > 0) {
    int n_events = vppcom_epoll_wait(epoll_handles[wrk_index], events, max_events, 0);
    if (n_events <= 0) {
      break;
    }
    max_events -= n_events;
    VCL_LOG("had {} events", n_events);

    for (int i = 0; i < n_events; i++) {
      auto vcl_handle = reinterpret_cast<VclIoHandle*>(events[i].data.u64);
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

uint32_t vclEpollHandle(uint32_t wrk_index) { return epoll_handles[wrk_index]; }

void vclInterfaceWorkerRegister() {
  {
    absl::MutexLock lk(&wrk_lock);
    RELEASE_ASSERT(vppcom_worker_register() == VPPCOM_OK, "failed to register VCL worker");
  }
  int epoll_handle = vppcom_epoll_create();
  epoll_handles[vppcom_worker_index()] = epoll_handle;
  VCL_LOG("registered worker {} and epoll handle {:x} mq fd {}", vppcom_worker_index(),
          epoll_handle, vppcom_mq_epoll_fd());
}

void vclInterfaceRegisterEpollEvent(Envoy::Event::Dispatcher& dispatcher) {
  MqFileEventsMap& mq_fevts_map = mqFileEventsMap();
  const int wrk_index = vppcom_worker_index();
  RELEASE_ASSERT(wrk_index != -1, "");
  if (mq_fevts_map.find(wrk_index) != mq_fevts_map.end()) {
    return;
  }
  mq_fevts_map[wrk_index] = dispatcher.createFileEvent(
      vppcom_mq_epoll_fd(), [](uint32_t events) -> void { onMqSocketEvents(events); },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Write);
}

void vclInterfaceInit(Event::Dispatcher& dispatcher) {
  MqFileEventsMap& mq_fevts_map = mqFileEventsMap();
  vppcom_app_create("envoy");
  const int wrk_index = vppcom_worker_index();
  epoll_handles[wrk_index] = vppcom_epoll_create();
  mq_fevts_map[wrk_index] = dispatcher.createFileEvent(
      vppcom_mq_epoll_fd(), [](uint32_t events) -> void { onMqSocketEvents(events); },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Write);
}

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy
