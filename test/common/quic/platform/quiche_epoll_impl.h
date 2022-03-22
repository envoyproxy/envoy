#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "quiche/epoll_server/simple_epoll_server.h"

namespace quiche {

using QuicheEpollServerImpl = epoll_server::SimpleEpollServer;
using QuicheEpollEventImpl = epoll_server::EpollEvent;
using QuicheEpollAlarmBaseImpl = epoll_server::EpollAlarm;
using QuicheEpollCallbackInterfaceImpl = epoll_server::EpollCallbackInterface;

} // namespace quiche
