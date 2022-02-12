#include "source/server/admin/logs_handler.h"

#include <string>

#include "source/common/common/fancy_logger.h"
#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/server/admin/utils.h"

#include "absl/strings/str_split.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Server {

LogsHandler::LogsHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code LogsHandler::handlerLogging(absl::string_view url, Http::ResponseHeaderMap&,
                                       Buffer::Instance& response, AdminStream&) {
  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);

  Http::Code rc = Http::Code::OK;
  if (!query_params.empty() && !changeLogLevel(query_params)) {
    response.add("usage: /logging?<name>=<level> (change single level)\n");
    response.add("usage: /logging?paths=name1:level1,name2:level2,... (change multiple levels)\n");
    response.add("usage: /logging?level=<level> (change all levels)\n");
    response.add("levels: ");
    for (auto level_string_view : spdlog::level::level_string_views) {
      response.add(fmt::format("{} ", level_string_view));
    }

    response.add("\n");
    rc = Http::Code::NotFound;
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

bool LogsHandler::changeLogLevel(const Http::Utility::QueryParams& params) {
  if (params.size() != 1) {
    return false;
  }

  const auto& it = params.begin();
  absl::string_view key{it.first};
  absl::string_view value{it.second};

  if (key == "all") {
    // Change all log levels.
    return changeAllLogLevels(value);
  } else if (key == "paths") {
    // Bulk change log level by name:level pairs, separated by comma.

    // Build a map of name:level pairs, a few allocations is ok here since it's
    // not common to call this function at a high rate.
    absl::flat_hash_map<absl::string_view, absl::string_view> name_levels;
    std::vector<absl::string_view> pairs = absl::StrSplit(value, ',', absl::SkipWhitespace());
    for (const auto& name_value : pairs) {
      std::pair<absl::string_view, absl::string_view> name_value_pair =
          absl::StrSplit(name_value, absl::MaxSplits(':', 1), absl::SkipWhitespace());
      auto [name, value] = name_value_pair;
      if (name.empty() || value.empty()) {
        continue;
      }

      name_levels.insert({name, value});
    }

    return changeLogLevels(name_levels);
  } else {
    // Change particular log level by name.
    return changeLogLevelByName(key, value);
  }
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

bool LogsHandler::changeAllLogLevels(absl::string_view level) {
  auto level_to_use = parseLevel(level);
  if (!level_to_use.has_value()) {
    ENVOY_LOG(error, "invalid log level specified: level='{}'", level);
    return false;
  }

  if (!Logger::Context::useFancyLogger()) {
    ENVOY_LOG(debug, "change all log levels: level='{}'", level);
    for (Logger::Logger& logger : Logger::Registry::loggers()) {
      logger.setLevel(*level_to_use);
    }
  } else {
    // Level setting with Fancy Logger.
    FANCY_LOG(info, "change all log levels: level='{}'", level);
    getFancyContext().setAllFancyLoggers(*level_to_use);
  }

  return true;
}

bool LogsHandler::changeLogLevels(
    const absl::flat_hash_map<absl::string_view, absl::string_view>& changes) {
  bool ret = true;
  for (auto [name, level] : changes) {
    ret = changeLogLevelByName(name, level) && ret;
  }
  return ret;
}

bool LogsHandler::changeLogLevelByName(absl::string_view name, absl::string_view level) {
  auto level_to_use = parseLevel(level);
  if (!level_to_use.has_value()) {
    ENVOY_LOG(error, "invalid log level specified: name='{}' level='{}'", name, level);
    return false;
  }

  if (!Logger::Context::useFancyLogger()) {
    ENVOY_LOG(debug, "change log level: name='{}' level='{}'", name, level);
    Logger::Logger* logger_to_change = nullptr;
    for (Logger::Logger& logger : Logger::Registry::loggers()) {
      if (logger.name() == name) {
        logger_to_change = &logger;
        break;
      }
    }

    if (!logger_to_change) {
      return false;
    }

    logger_to_change->setLevel(*level_to_use);
  } else {
    // Level setting with Fancy Logger.
    FANCY_LOG(info, "change log level: name='{}' level='{}'", name, level);
    bool res = getFancyContext().setFancyLogger(std::string(name), *level_to_use);
    return res;
  }

  return true;
}

} // namespace Server
} // namespace Envoy
