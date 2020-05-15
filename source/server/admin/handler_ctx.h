#pragma once

#include "envoy/server/instance.h"

namespace Envoy {
namespace Server {

class HandlerContextBase {
public:
  HandlerContextBase(Server::Instance& server) : server_(server) {}

protected:
  Server::Instance& server_;
};

} // namespace Server
} // namespace Envoy
