#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_epoll_impl.h"

namespace quic {

using QuicEpollServerImpl = ::quiche::EpollServer;
using QuicEpollEventImpl = ::quiche::EpollEvent;
using QuicEpollAlarmBaseImpl = ::quiche::EpollAlarm;
using QuicEpollCallbackInterfaceImpl = ::quiche::EpollCallbackInterface;

} // namespace quic
