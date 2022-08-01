#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "source/server/admin/handler_ctx.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
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
  /**
   * Attempt to change the log level of a logger or all loggers.
   *
   * Returns StatusCode::kInvalidArgument if validation failed.
   *
   * @param params supplies the incoming endpoint query params.
   */
  absl::Status changeLogLevel(const Http::Utility::QueryParams& params);
  void changeAllLogLevels(spdlog::level::level_enum level);
  absl::Status
  changeLogLevels(const absl::flat_hash_map<absl::string_view, spdlog::level::level_enum>& changes);

  inline absl::StatusOr<spdlog::level::level_enum> parseLogLevel(absl::string_view level_string) {
    auto level_it = log_levels_.find(level_string);
    if (level_it == log_levels_.end()) {
      return absl::InvalidArgumentError("unknown logger level");
    }
    return level_it->second;
  }

  // Maps level string to level enum.
  const absl::flat_hash_map<absl::string_view, spdlog::level::level_enum> log_levels_;
};

} // namespace Server
} // namespace Envoy
