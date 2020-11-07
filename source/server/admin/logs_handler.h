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

class LogsHandler : public HandlerContextBase, Logger::Loggable<Logger::Id::admin> {

public:
  LogsHandler(Server::Instance& server);

  Http::Code handlerLogging(absl::string_view path_and_query,
                            Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                            AdminStream&);

  Http::Code handlerReopenLogs(absl::string_view path_and_query,
                               Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response, AdminStream&);

private:
  /**
   * Attempt to change the log level of a logger or all loggers
   * @param params supplies the incoming endpoint query params.
   * @return TRUE if level change succeeded, FALSE otherwise.
   */
  bool changeLogLevel(const Http::Utility::QueryParams& params);
};

} // namespace Server
} // namespace Envoy
