#pragma once

#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "server/admin/handler_ctx.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class RuntimeHandler : public HandlerContextBase {

public:
  RuntimeHandler(Server::Instance& server);

  Http::Code handlerRuntime(absl::string_view path_and_query,
                            Http::ResponseHeaderMap& response_headers, Chunker& response,
                            AdminStream&);
  Http::Code handlerRuntimeModify(absl::string_view path_and_query,
                                  Http::ResponseHeaderMap& response_headers,
                                  Chunker& response, AdminStream&);
};

} // namespace Server
} // namespace Envoy
