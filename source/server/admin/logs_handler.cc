#include "source/server/admin/logs_handler.h"

#include <string>

#include "source/common/common/fancy_logger.h"
#include "source/common/common/logger.h"
#include "source/server/admin/utils.h"

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Server {

namespace {
// Build the level string to level enum map.
absl::flat_hash_map<absl::string_view, spdlog::level::level_enum> buildLevelMap() {
  absl::flat_hash_map<absl::string_view, spdlog::level::level_enum> levels;

  for (size_t i = 0; i < ARRAY_SIZE(spdlog::level::level_string_views); i++) {
    spdlog::string_view_t spd_level_string{spdlog::level::level_string_views[i]};
    absl::string_view level_string{spd_level_string.data(), spd_level_string.size()};
    levels[level_string] = static_cast<spdlog::level::level_enum>(i);
  }

  return levels;
}
} // namespace

LogsHandler::LogsHandler(Server::Instance& server)
    : HandlerContextBase(server), log_levels_(buildLevelMap()) {}

Http::Code LogsHandler::handlerLogging(absl::string_view url, Http::ResponseHeaderMap&,
                                       Buffer::Instance& response, AdminStream&) {
  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);

  Http::Code rc = Http::Code::OK;
  if (!query_params.empty()) {
    auto status = changeLogLevel(query_params);
    if (!status.ok()) {
      rc = Http::Code::BadRequest;
      response.add(fmt::format("error: {}\n\n", status.message()));

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

absl::Status LogsHandler::changeLogLevel(const Http::Utility::QueryParams& params) {
  if (params.size() != 1) {
    return absl::InvalidArgumentError("invalid number of parameters");
  }

  const auto it = params.begin();
  absl::string_view key(it->first);
  absl::string_view value(it->second);

  if (key == "level") {
    // Change all log levels.
    auto level_to_use = parseLogLevel(value);
    if (!level_to_use.ok()) {
      return level_to_use.status();
    }

    changeAllLogLevels(*level_to_use);
    return absl::OkStatus();
  }

  // Build a map of name:level pairs, a few allocations is ok here since it's
  // not common to call this function at a high rate.
  absl::flat_hash_map<absl::string_view, spdlog::level::level_enum> name_levels;

  if (key == "paths") {
    // Bulk change log level by name:level pairs, separated by comma.
    std::vector<absl::string_view> pairs = absl::StrSplit(value, ',', absl::SkipWhitespace());
    for (const auto& name_level : pairs) {
      std::pair<absl::string_view, absl::string_view> name_level_pair =
          absl::StrSplit(name_level, absl::MaxSplits(':', 1), absl::SkipWhitespace());
      auto [name, level] = name_level_pair;
      if (name.empty() || level.empty()) {
        return absl::InvalidArgumentError("empty logger name or empty logger level");
      }

      auto level_to_use = parseLogLevel(level);
      if (!level_to_use.ok()) {
        return level_to_use.status();
      }

      name_levels[name] = *level_to_use;
    }
  } else {
    // Change particular log level by name.
    auto level_to_use = parseLogLevel(value);
    if (!level_to_use.ok()) {
      return level_to_use.status();
    }

    name_levels[key] = *level_to_use;
  }

  return changeLogLevels(name_levels);
}

void LogsHandler::changeAllLogLevels(spdlog::level::level_enum level) {
  if (!Logger::Context::useFancyLogger()) {
    ENVOY_LOG(info, "change all log levels: level='{}'", spdlog::level::level_string_views[level]);
    Logger::Registry::setLogLevel(level);
  } else {
    // Level setting with Fancy Logger.
    FANCY_LOG(info, "change all log levels: level='{}'", spdlog::level::level_string_views[level]);
    getFancyContext().setAllFancyLoggers(level);
  }
}

absl::Status LogsHandler::changeLogLevels(
    const absl::flat_hash_map<absl::string_view, spdlog::level::level_enum>& changes) {
  if (!Logger::Context::useFancyLogger()) {
    std::vector<std::pair<Logger::Logger*, spdlog::level::level_enum>> loggers_to_change;
    for (Logger::Logger& logger : Logger::Registry::loggers()) {
      auto name_level_itr = changes.find(logger.name());
      if (name_level_itr == changes.end()) {
        continue;
      }

      loggers_to_change.emplace_back(std::make_pair(&logger, name_level_itr->second));
    }

    // Check if we have any invalid logger in changes.
    if (loggers_to_change.size() != changes.size()) {
      return absl::InvalidArgumentError("unknown logger name");
    }

    for (auto& it : loggers_to_change) {
      Logger::Logger* logger = it.first;
      spdlog::level::level_enum level = it.second;

      ENVOY_LOG(info, "change log level: name='{}' level='{}'", logger->name(),
                spdlog::level::level_string_views[level]);
      logger->setLevel(level);
    }
  } else {
    std::vector<std::pair<SpdLoggerSharedPtr, spdlog::level::level_enum>> loggers_to_change;
    for (auto& it : changes) {
      // TODO(timonwong) FancyContext::getFancyLogEntry should accept absl::string_view as key.
      SpdLoggerSharedPtr logger = getFancyContext().getFancyLogEntry(std::string(it.first));
      if (!logger) {
        return absl::InvalidArgumentError("unknown logger name");
      }

      loggers_to_change.emplace_back(std::make_pair(logger, it.second));
    }

    for (auto& it : loggers_to_change) {
      SpdLoggerSharedPtr logger = it.first;
      spdlog::level::level_enum level = it.second;

      FANCY_LOG(info, "change log level: name='{}' level='{}'", logger->name(),
                spdlog::level::level_string_views[level]);
      logger->set_level(level);
    }
  }

  return absl::OkStatus();
}

} // namespace Server
} // namespace Envoy
