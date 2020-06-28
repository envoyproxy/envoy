#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "server/admin/handler_ctx.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class ServerInfoHandler : public HandlerContextBase {

public:
  ServerInfoHandler(Server::Instance& server);

  Http::Code handlerCerts(absl::string_view path_and_query,
                          Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                          AdminStream&);

  Http::Code handlerServerInfo(absl::string_view path_and_query,
                               Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response, AdminStream&);

  Http::Code handlerReady(absl::string_view path_and_query,
                          Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                          AdminStream&);

  Http::Code handlerHotRestartVersion(absl::string_view path_and_query,
                                      Http::ResponseHeaderMap& response_headers,
                                      Buffer::Instance& response, AdminStream&);

  Http::Code handlerMemory(absl::string_view path_and_query,
                           Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                           AdminStream&);
};

} // namespace Server
} // namespace Envoy
