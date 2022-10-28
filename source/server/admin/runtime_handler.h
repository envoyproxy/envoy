#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "source/server/admin/handler_ctx.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class RuntimeHandler : public HandlerContextBase {

public:
  RuntimeHandler(Server::Instance& server);

  Http::Code handlerRuntime(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                            AdminStream&);
  Http::Code handlerRuntimeModify(Http::ResponseHeaderMap& response_headers,
                                  Buffer::Instance& response, AdminStream&);
};

} // namespace Server
} // namespace Envoy
