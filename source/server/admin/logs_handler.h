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

  Http::Code handlerLogging(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                            AdminStream&);

  Http::Code handlerReopenLogs(Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response, AdminStream&);

  /**
   * Returns the valid logging levels as an array of string views.
   */
  static std::vector<absl::string_view> levelStrings();

private:
  /**
   * Attempt to change the log level of a logger or all loggers.
   *
   * @return OkStatus if there are no non-empty params, StatusCode::kInvalidArgument if
   * validation failed.
   *
   * @param params supplies the incoming endpoint query or post params.
   */
  absl::Status changeLogLevel(Http::Utility::QueryParamsMulti& params);
  absl::Status changeLogLevelsForComponentLoggers(
      const absl::flat_hash_map<absl::string_view, spdlog::level::level_enum>& changes);

  inline absl::StatusOr<spdlog::level::level_enum> parseLogLevel(absl::string_view level_string) {
    auto level_it = log_levels_.find(level_string);
    if (level_it == log_levels_.end()) {
      return absl::InvalidArgumentError("unknown logger level");
    }
    return level_it->second;
  }

  // Maps level string to level enum.
  using StringViewLevelMap = absl::flat_hash_map<absl::string_view, spdlog::level::level_enum>;
  const StringViewLevelMap log_levels_;
};

} // namespace Server
} // namespace Envoy
