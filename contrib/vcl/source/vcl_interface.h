#pragma once

#include "envoy/network/socket.h"

#include "source/common/network/socket_interface.h"

#include "vppcom.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace Vcl {

#define VCL_DEBUG (0)
#define VCL_RX_ZC (0)

#if VCL_DEBUG > 0
#define VCL_LOG(fmt, _args...) ENVOY_LOG_MISC(debug, "[{}] " fmt, vppcom_worker_index(), ##_args)
#else
#define VCL_LOG(fmt, _args...)
#endif

/**
 * VclIoHandle does not rely on linux fds. Constant lower used as invalid fd.
 */
const int VclInvalidFd = 1 << 23;

/**
 * Used to initialize VCL interface when VclSocketInterface extension is loaded.
 */
void vclInterfaceInit(Event::Dispatcher& dispatcher);

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

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy
