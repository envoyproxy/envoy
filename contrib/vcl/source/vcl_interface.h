#pragma once

#include "envoy/network/socket.h"

#include "source/common/network/socket_interface.h"

#include "vppcom.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace Vcl {

#define VCL_RX_ZC (0)
#define VCL_LOG(fmt, _args...) ENVOY_LOG_MISC(debug, "[{}] " fmt, vppcom_worker_index(), ##_args)

/**
 * VclIoHandle does not rely on linux fds. Constant lower used as invalid fd.
 */
constexpr int VclInvalidFd = 1 << 23;

/**
 * Used to initialize VCL interface when VclSocketInterface extension is loaded.
 */
void vclInterfaceInit(Event::Dispatcher& dispatcher, uint32_t concurrency);

/**
 * Register Envoy worker with VCL and allocate epoll session handle to be used to retrieve per
 * worker session events.
 */
void vclInterfaceWorkerRegister();

/**
 * Create FileEvent for VCL worker message queue `eventfd` if one does not exist. Used to signal
 * main dispatch loop that VCL has session events.
 */
void vclInterfaceRegisterEpollEvent(Envoy::Event::Dispatcher& dispatcher);

/**
 * Retrieve epoll session handle for VCL worker.
 */
uint32_t vclEpollHandle(uint32_t wrk_index);

/**
 * Force drain of events on current worker
 */
void vclInterfaceDrainEvents();

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy
