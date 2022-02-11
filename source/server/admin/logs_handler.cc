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

Http::Code LogsHandler::handlerLogging(absl::string_view url, Http::ResponseHeaderMap&,
                                       Buffer::Instance& response, AdminStream&) {
  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);

  Http::Code rc = Http::Code::OK;
  if (!query_params.empty() && !changeLogLevel(query_params)) {
    response.add("usage: /logging?<name>=<level> (change single level)\n");
    response.add("usage: /logging?paths=name1:level1,name2,level2,... (change multiple levels)\n");
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

  const auto it = params.begin();
  absl::string_view key = it->first;
  absl::string_view value = it->secondn;

  if (key == "paths") {
    // Bulk change log level by name:level pairs
    std::vector<absl::string_view> pairs = absl::StrSplit(value, ',', absl::SkipWhitespace());
    bool ret = true;
    for (const auto& name_value : pairs) {
      std::pair<absl::string_view, absl::string_view> name_value_pair =
          absl::StrSplit(name_value, absl::MaxSplits(':', 1), absl::SkipWhitespace());

      auto name = name_value_pair.first;
      auto value = name_value_pair.second;

      if (name.empty() || value.empty()) {
        continue;
      }

      ret = changeLogLevelByName(name, value) && ret;
    }

    return ret;
  } else if (key == "level") {
    // Change all log levels
    return changeAllLogLevels(value);
  } else {
    // Change specific log level by its name
    return changeLogLevelByName(key, value);
  }
}

static bool parseLevel(absl::string_view level, spdlog::level::level_enum& level_to_use) {
  std::string_view level_string_view = toStdStringView(level);
  for (size_t i = 0; i < ARRAY_SIZE(spdlog::level::level_string_views); i++) {
    if (level_string_view == spdlog::level::level_string_views[i]) {
      level_to_use = static_cast<spdlog::level::level_enum>(i);
      return true;
    }
  }

  return false;
}

bool LogsHandler::changeAllLogLevels(absl::string_view level) {
  spdlog::level::level_enum level_to_use;
  if (!parseLevel(level, level_to_use)) {
    return false;
  }

  if (!Logger::Context::useFancyLogger()) {
    ENVOY_LOG(debug, "change all log levels: level='{}'", level);
    for (Logger::Logger& logger : Logger::Registry::loggers()) {
      logger.setLevel(level_to_use);
    }
  } else {
    // Level setting with Fancy Logger.
    FANCY_LOG(info, "change all log levels: level='{}'", level);
    getFancyContext().setAllFancyLoggers(level_to_use);
  }

  return true;
}

bool LogsHandler::changeLogLevelByName(absl::string_view name, absl::string_view level) {
  spdlog::level::level_enum level_to_use;
  if (!parseLevel(level, level_to_use)) {
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

    logger_to_change->setLevel(level_to_use);
  } else {
    // Level setting with Fancy Logger.
    FANCY_LOG(info, "change log level: name='{}' level='{}'", name, level);
    bool res = getFancyContext().setFancyLogger(name, level_to_use);
    return res;
  }

  return true;
}

} // namespace Server
} // namespace Envoy
