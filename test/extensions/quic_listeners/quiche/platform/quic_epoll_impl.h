#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "quiche/epoll_server/simple_epoll_server.h"

namespace quic {

using QuicEpollServerImpl = epoll_server::SimpleEpollServer;
using QuicEpollEventImpl = epoll_server::EpollEvent;
using QuicEpollAlarmBaseImpl = epoll_server::EpollAlarm;
using QuicEpollCallbackInterfaceImpl = epoll_server::EpollCallbackInterface;

} // namespace quic
