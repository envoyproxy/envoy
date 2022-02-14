#pragma once

#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "source/server/admin/handler_ctx.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "spdlog/spdlog.h"

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
  using LogLevelMap = absl::flat_hash_map<absl::string_view, spdlog::level::level_enum>;

  /**
   * Attempt to change the log level of a logger or all loggers
   * @param params supplies the incoming endpoint query params.
   * @return (TRUE, "") if level change succeeded, (FALSE, "error message") otherwise.
   */
  std::pair<bool, std::string> changeLogLevel(const Http::Utility::QueryParams& params);
  void changeAllLogLevels(spdlog::level::level_enum level));
  std::pair<bool, std::string> changeLogLevels(const LogLevelMap& changes);
};

} // namespace Server
} // namespace Envoy
