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
  // Always set Content-Type and X-Content-Type-Options to prevent XSS attacks
  response_headers.addReference(Http::Headers::get().XContentTypeOptions,
                                Http::Headers::get().XContentTypeOptionValues.Nosniff);
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Text);

  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);

  Http::Code rc = Http::Code::OK;
  ErrorMessages errors;
  if (!changeLogLevel(query_params, errors)) {
    rc = Http::Code::BadRequest;
    if (!errors.empty()) {
      response.add("errors:\n");
      for (const auto& err : errors) {
        response.add(fmt::format("- {}\n", err));
      }
      response.add("\n")
    }

    response.add("usage: /logging?<name>=<level> (change single level)\n");
    response.add("usage: /logging?paths=name1:level1,name2:level2,... (change multiple levels)\n");
    response.add("usage: /logging?level=<level> (change all levels)\n");
    response.add("levels: ");
    for (auto level_string_view : spdlog::level::level_string_views) {
      response.add(fmt::format("{} ", level_string_view));
    }

    response.add("\n");
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

bool LogsHandler::changeLogLevel(const Http::Utility::QueryParams& params, ErrorMessages& errors) {
  if (params.size() != 1) {
    return absl::make_optional({});
  }

  const auto it = params.begin();
  absl::string_view key(it->first);
  absl::string_view value(it->second);

  if (key == "all") {
    // Change all log levels.
    auto level_to_use = parseLevel(level);
    if (!level_to_use.has_value()) {
      errors.emplace_back(
          fmt::format("change all log levels: invalid log level: level='{}'", name, level));
      return false;
    }

    changeAllLogLevels(value);
    return true;
  }

  // Build a map of name:level pairs, a few allocations is ok here since it's
  // not common to call this function at a high rate.
  LogLevelMap name_levels;
  ErrorMessages errors;

  if (key == "paths") {
    // Bulk change log level by name:level pairs, separated by comma.
    std::vector<absl::string_view> pairs = absl::StrSplit(value, ',', absl::SkipWhitespace());
    for (const auto& name_level : pairs) {
      std::pair<absl::string_view, absl::string_view> name_level_pair =
          absl::StrSplit(name_level, absl::MaxSplits(':', 1), absl::SkipWhitespace());
      auto [name, level] = name_level_pair;

      auto level_to_use = parseLevel(level);
      if (!level_to_use.has_value()) {
        errors.emplace_back(fmt::format(
            "bulk change log level: invalid log level: name='{}' level='{}'", name, level));
        continue;
      }

      name_levels[name] = *level_to_use;
    }
  } else {
    // Change particular log level by name.
    auto level_to_use = parseLevel(level);
    if (!level_to_use.has_value()) {
      return absl.make_optional(
          {fmt::format("change particular log level: invalid log level: level='{}'", level});
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

bool LogsHandler::changeLogLevels(const LogLevelMap& changes, ErrorMessages errors) {
  bool all_success = true;

  if (!Logger::Context::useFancyLogger()) {
    // Build a map of name to logger.
    absl::flat_hash_map<std::string, Logger::Logger*> loggers;
    for (auto&& logger : Logger::Registry::loggers()) {
      loggers[logger.name()] = &logger;
    }

    for (const auto [name, level] : changes) {
      auto it = loggers.find(name);
      if (it == loggers.end()) {
        all_success = false;
        errors.emplace_back(fmt::format("change log level: unknown logger: name='{}'", name));
        continue;
      }

      ENVOY_LOG(debug, "change log level: name='{}' level='{}'", name,
                spdlog::level::level_string_views[level]);
      it->second->setLevel(level);
    }
  } else {
    // Level setting with Fancy Logger.
    for (const auto [name, level] : changes) {
      FANCY_LOG(info, "change log level: name='{}' level='{}'", name,
                spdlog::level::level_string_views[level]);

      bool res = getFancyContext().setFancyLogger(std::string(name), *level_to_use);
      if (!res) {
        all_success = false;
        errors.emplace_back(fmt::format("change log level: unknown logger: name='{}'", name));
      }
    }
  }

  return all_success;
}

} // namespace Server
} // namespace Envoy
