#include "source/server/admin/logs_handler.h"

#include <string>

#include "source/common/common/fancy_logger.h"
#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/server/admin/utils.h"

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Server {

LogsHandler::LogsHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code LogsHandler::handlerLogging(absl::string_view url,
                                       Http::ResponseHeaderMap& response_headers,
                                       Buffer::Instance& response, AdminStream&) {
  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);

  Http::Code rc = Http::Code::OK;
  if (!query_params.empty()) {
    const auto& [success, error] = changeLogLevel(query_params);
    if (!success) {
      rc = Http::Code::BadRequest;
      if (!error.empty()) {
        response.add(fmt::format("error: {}\n\n", error));
      }

      response.add("usage: /logging?<name>=<level> (change single level)\n");
      response.add(
          "usage: /logging?paths=name1:level1,name2:level2,... (change multiple levels)\n");
      response.add("usage: /logging?level=<level> (change all levels)\n");
      response.add("levels: ");
      for (auto level_string_view : spdlog::level::level_string_views) {
        response.add(fmt::format("{} ", level_string_view));
      }

      response.add("\n");
    }
  }

  if (!Logger::Context::useFancyLogger()) {
    response.add("active loggers:\n");
    for (const Logger::Logger& logger : Logger::Registry::loggers()) {
      response.add(fmt::format("  {}: {}\n", logger.name(), logger.levelString()));
    }

    response.add("\n");
  } else {
    response.add("active loggers:\n");
    std::string logger_info = getFancyContext().listFancyLoggers();
    response.add(logger_info);
  }

  return rc;
}

Http::Code LogsHandler::handlerReopenLogs(absl::string_view, Http::ResponseHeaderMap&,
                                          Buffer::Instance& response, AdminStream&) {
  server_.accessLogManager().reopen();
  response.add("OK\n");
  return Http::Code::OK;
}

static absl::optional<spdlog::level::level_enum> parseLevel(absl::string_view level) {
  for (size_t i = 0; i < ARRAY_SIZE(spdlog::level::level_string_views); i++) {
    spdlog::string_view_t spd_log_level{spdlog::level::level_string_views[i]};
    if (level == absl::string_view{spd_log_level.data(), spd_log_level.size()}) {
      return static_cast<spdlog::level::level_enum>(i);
    }
  }

  return absl::nullopt;
}

std::pair<bool, std::string> LogsHandler::changeLogLevel(const Http::Utility::QueryParams& params) {
  if (params.size() != 1) {
    return std::make_pair(false, "invalid number of parameters");
  }

  const auto it = params.begin();
  absl::string_view key(it->first);
  absl::string_view value(it->second);

  if (key == "all") {
    // Change all log levels.
    auto level_to_use = parseLevel(level);
    if (!level_to_use.has_value()) {
      return std::make_pair(false, "unknown log level");
    }

    changeAllLogLevels(value);
    return std::make_pair(true, "");
  }

  // Build a map of name:level pairs, a few allocations is ok here since it's
  // not common to call this function at a high rate.
  LogLevelMap name_levels;

  if (key == "paths") {
    // Bulk change log level by name:level pairs, separated by comma.
    std::vector<absl::string_view> pairs = absl::StrSplit(value, ',', absl::SkipWhitespace());
    for (const auto& name_level : pairs) {
      std::pair<absl::string_view, absl::string_view> name_level_pair =
          absl::StrSplit(name_level, absl::MaxSplits(':', 1), absl::SkipWhitespace());
      auto [name, level] = name_level_pair;
      if (name.empty() || level.empty()) {
        return std::make_pair(false, "empty log name or empty log level");
      }

      auto level_to_use = parseLevel(level);
      if (!level_to_use.has_value()) {
        return std::make_pair(false, "unknown log level");
      }

      name_levels[name] = *level_to_use;
    }
  } else {
    // Change particular log level by name.
    auto level_to_use = parseLevel(level);
    if (!level_to_use.has_value()) {
      return std::make_pair(false, "unknown log level");
    }

    name_levels[name] = *level_to_use;
  }

  return changeLogLevels(name_levels, errors);
}

void LogsHandler::changeAllLogLevels(spdlog::level::level_enum level) {
  if (!Logger::Context::useFancyLogger()) {
    ENVOY_LOG(debug, "change all log levels: level='{}'", spdlog::level::level_string_views[level]);
    Logger::Registry::setLogLevel(*level_to_use);
  } else {
    // Level setting with Fancy Logger.
    FANCY_LOG(info, "change all log levels: level='{}'", spdlog::level::level_string_views[level]);
    getFancyContext().setAllFancyLoggers(*level_to_use);
  }
}

std::pair<bool, std::string> LogsHandler::changeLogLevels(const LogLevelMap& changes) {
  if (!Logger::Context::useFancyLogger()) {
    // Build a map of name to logger.
    absl::flat_hash_map<std::string, Logger::Logger*> loggers;
    for (auto&& logger : Logger::Registry::loggers()) {
      loggers[logger.name()] = &logger;
    }

    // Validate first
    for (const auto [name, level] : changes) {
      if (loggers.find(name) == loggers.end()) {
        return std::make_pair(false, fmt::format("unknown log name"));
      }
    }

    for (const auto [name, level] : changes) {
      ENVOY_LOG(debug, "change log level: name='{}' level='{}'", name,
                spdlog::level::level_string_views[level]);
      loggers[name]->setLevel(level);
    }
  } else {
    // Level setting with Fancy Logger.
    auto names = getFancyContext().getFancyLoggerKeys();

    // Validate first
    for (const auto [name, level] : changes) {
      if (names.find(name) == names.end()) {
        return std::make_pair(false, fmt::format("unknown log name"));
      }
    }

    for (const auto [name, level] : changes) {
      FANCY_LOG(info, "change log level: name='{}' level='{}'", name,
                spdlog::level::level_string_views[level]);

      getFancyContext().setFancyLogger(std::string(name), *level_to_use);
    }
  }

  return std::make_pair(true, "");
}

} // namespace Server
} // namespace Envoy
