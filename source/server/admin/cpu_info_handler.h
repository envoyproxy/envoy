#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "source/server/admin/handler_ctx.h"

namespace Envoy {
namespace Server {

class CpuInfoHandler : public HandlerContextBase {
public:
  CpuInfoHandler(Server::Instance& server);

  Http::Code handlerWorkersCpu(Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response, AdminStream& admin_stream);
};

} // namespace Server
} // namespace Envoy


