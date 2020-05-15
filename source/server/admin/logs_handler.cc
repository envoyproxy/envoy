#include "server/admin/logs_handler.h"

#include <string>

#include "common/common/logger.h"

#include "server/admin/utils.h"

namespace Envoy {
namespace Server {

LogsHandler::LogsHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code LogsHandler::handlerLogging(absl::string_view url, Http::ResponseHeaderMap&,
                                       Buffer::Instance& response, AdminStream&) {
  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);

  Http::Code rc = Http::Code::OK;
  if (!query_params.empty() && !changeLogLevel(query_params)) {
    response.add("usage: /logging?<name>=<level> (change single level)\n");
    response.add("usage: /logging?level=<level> (change all levels)\n");
    response.add("levels: ");
    for (auto level_string_view : spdlog::level::level_string_views) {
      response.add(fmt::format("{} ", level_string_view));
    }

    response.add("\n");
    rc = Http::Code::NotFound;
  }

  response.add("active loggers:\n");
  for (const Logger::Logger& logger : Logger::Registry::loggers()) {
    response.add(fmt::format("  {}: {}\n", logger.name(), logger.levelString()));
  }

  response.add("\n");
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

  std::string name = params.begin()->first;
  std::string level = params.begin()->second;

  // First see if the level is valid.
  size_t level_to_use = std::numeric_limits<size_t>::max();
  for (size_t i = 0; i < ARRAY_SIZE(spdlog::level::level_string_views); i++) {
    if (level == spdlog::level::level_string_views[i]) {
      level_to_use = i;
      break;
    }
  }

  if (level_to_use == std::numeric_limits<size_t>::max()) {
    return false;
  }

  // Now either change all levels or a single level.
  if (name == "level") {
    ENVOY_LOG(debug, "change all log levels: level='{}'", level);
    for (Logger::Logger& logger : Logger::Registry::loggers()) {
      logger.setLevel(static_cast<spdlog::level::level_enum>(level_to_use));
    }
  } else {
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

    logger_to_change->setLevel(static_cast<spdlog::level::level_enum>(level_to_use));
  }

  return true;
}

} // namespace Server
} // namespace Envoy
