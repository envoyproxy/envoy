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

void vclInterfaceInit(Event::Dispatcher& dispatcher);
void vclInterfaceWorkerRegister();
void vclInterfaceRegisterEpollEvent(Envoy::Event::Dispatcher& dispatcher);
uint32_t& vclEpollHandle(uint32_t wrk_index);

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy
