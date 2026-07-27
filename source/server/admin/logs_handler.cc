#include "source/server/admin/logs_handler.h"

#include <string>
#include <vector>

#include "source/common/common/fine_grain_logger.h"
#include "source/common/common/logger.h"
#include "source/server/admin/utils.h"

#include "absl/strings/str_split.h"
#include "fmt/format.h"

namespace Envoy {
namespace Server {

namespace {
// Build the level string to level enum map.
absl::flat_hash_map<absl::string_view, spdlog::level::level_enum> buildLevelMap() {
  absl::flat_hash_map<absl::string_view, spdlog::level::level_enum> levels;

  uint32_t i = 0;
  for (absl::string_view level_string : LogsHandler::levelStrings()) {
    levels[level_string] = static_cast<spdlog::level::level_enum>(i++);
  }

  return levels;
}
} // namespace

LogsHandler::LogsHandler(Server::Instance& server)
    : HandlerContextBase(server), log_levels_(buildLevelMap()) {}

std::vector<absl::string_view> LogsHandler::levelStrings() {
  std::vector<absl::string_view> strings;
  strings.reserve(ARRAY_SIZE(spdlog::level::level_string_views));
  for (spdlog::string_view_t level : spdlog::level::level_string_views) {
    strings.emplace_back(absl::string_view{level.data(), level.size()});
  }
  return strings;
}

Http::Code LogsHandler::handlerLogging(Http::ResponseHeaderMap&, Buffer::Instance& response,
                                       AdminStream& admin_stream) {
  Http::Utility::QueryParamsMulti query_params = admin_stream.queryParams();

  Http::Code rc = Http::Code::OK;
  const absl::Status status = changeLogLevel(query_params);
  if (!status.ok()) {
    rc = Http::Code::BadRequest;
    response.add(fmt::format("error: {}\n\n", status.message()));

    response.add("usage: /logging?<name>=<level> (change single level)\n");
    response.add("usage: /logging?paths=name1:level1,name2:level2,... (change multiple levels)\n");
    response.add("usage: /logging?level=<level> (change all levels)\n");
    response.add("usage: /logging?group=<group_name>:<level> (change group of loggers)\n");
    response.add("levels: ");
    for (auto level_string_view : spdlog::level::level_string_views) {
      response.add(fmt::format("{} ", level_string_view));
    }

    response.add("\n");
  }

  if (!Logger::Context::useFineGrainLogger()) {
    response.add("active loggers:\n");
    for (const Logger::Logger& logger : Logger::Registry::loggers()) {
      response.add(fmt::format("  {}: {}\n", logger.name(), logger.levelString()));
    }

    response.add("\n");
  } else {
    response.add("active loggers:\n");
    std::string logger_info = getFineGrainLogContext().listFineGrainLoggers();
    response.add(logger_info);
    response.add("\n");
  }

  return rc;
}

Http::Code LogsHandler::handlerReopenLogs(Http::ResponseHeaderMap&, Buffer::Instance& response,
                                          AdminStream&) {
  server_.accessLogManager().reopen();
  response.add("OK\n");
  return Http::Code::OK;
}

absl::Status LogsHandler::changeLogLevel(Http::Utility::QueryParamsMulti& params) {
  // Identify active non-empty parameters.
  std::string active_key;
  std::string active_value;
  int active_params_count = 0;

  for (auto const& [key, values] : params.data()) {
    if (values.empty() || values[0].empty()) {
      continue;
    }
    active_params_count++;
    active_key = key;
    active_value = values[0];
  }

  if (active_params_count == 0) {
    return absl::OkStatus();
  }

  if (active_params_count != 1) {
    return absl::InvalidArgumentError("invalid number of parameters");
  }

  const bool use_fine_grain_logger = Logger::Context::useFineGrainLogger();

  if (active_key == "level") {
    // Change all log levels.
    const absl::StatusOr<spdlog::level::level_enum> level_to_use = parseLogLevel(active_value);
    if (!level_to_use.ok()) {
      return level_to_use.status();
    }

    Logger::Context::changeAllLogLevels(*level_to_use);
    return absl::OkStatus();
  }

  // Build a map of name:level pairs, a few allocations is ok here since it's
  // not common to call this function at a high rate.
  absl::flat_hash_map<absl::string_view, spdlog::level::level_enum> name_levels;
  std::vector<FineGrainLogContext::VerbosityUpdate> glob_levels;

  if (active_key == "paths") {
    // Bulk change log level by name:level pairs, separated by comma.
    std::vector<absl::string_view> pairs =
        absl::StrSplit(active_value, ',', absl::SkipWhitespace());
    for (const auto& name_level : pairs) {
      const std::pair<absl::string_view, absl::string_view> name_level_pair =
          absl::StrSplit(name_level, absl::MaxSplits(':', 1), absl::SkipWhitespace());
      auto [name, level_str] = name_level_pair;
      if (name.empty() || level_str.empty()) {
        return absl::InvalidArgumentError("empty logger name or empty logger level");
      }

      const absl::StatusOr<spdlog::level::level_enum> level_to_use = parseLogLevel(level_str);
      if (!level_to_use.ok()) {
        return level_to_use.status();
      }

      if (use_fine_grain_logger) {
        ENVOY_LOG(info, "adding fine-grain log update, pattern='{}' level='{}'", name,
                  spdlog::level::level_string_views[*level_to_use]);
        glob_levels.push_back({name, static_cast<int>(*level_to_use), false});
      } else {
        name_levels[name] = *level_to_use;
      }
    }
  } else if (active_key == "group") {
    // Group parameter requires fine-grain logging to be enabled
    if (!use_fine_grain_logger) {
      return absl::InvalidArgumentError(
          "group parameter requires fine-grain logging to be enabled");
    }
    const std::pair<absl::string_view, absl::string_view> name_level_pair =
        absl::StrSplit(active_value, absl::MaxSplits(':', 1), absl::SkipWhitespace());
    auto [name, level_str] = name_level_pair;
    if (name.empty() || level_str.empty()) {
      return absl::InvalidArgumentError("empty logger name or empty logger level in group");
    }
    const absl::StatusOr<spdlog::level::level_enum> level_to_use = parseLogLevel(level_str);
    if (!level_to_use.ok()) {
      return level_to_use.status();
    }
    ENVOY_LOG(info, "adding fine-grain log update, group='{}' level='{}'", name,
              spdlog::level::level_string_views[*level_to_use]);
    glob_levels.push_back({name, static_cast<int>(*level_to_use), true});
  } else {
    // Change particular log level by name (legacy non-HTML mechanism).
    const absl::StatusOr<spdlog::level::level_enum> level_to_use = parseLogLevel(active_value);
    if (!level_to_use.ok()) {
      return level_to_use.status();
    }

    if (use_fine_grain_logger) {
      ENVOY_LOG(info, "adding fine-grain log update, pattern='{}' level='{}'", active_key,
                spdlog::level::level_string_views[*level_to_use]);
      glob_levels.push_back({active_key, static_cast<int>(*level_to_use), false});
    } else {
      if (active_key == "level" || active_key == "paths" || active_key == "group") {
        if (active_key == "group") {
          return absl::InvalidArgumentError(
              "group parameter requires fine-grain logging to be enabled");
        }
        return absl::InvalidArgumentError(fmt::format("invalid parameter: {}", active_key));
      }
      name_levels[active_key] = *level_to_use;
    }
  }

  if (!use_fine_grain_logger) {
    return changeLogLevelsForComponentLoggers(name_levels);
  }
  getFineGrainLogContext().updateVerbositySetting(glob_levels);

  return absl::OkStatus();
}

absl::Status LogsHandler::changeLogLevelsForComponentLoggers(
    const absl::flat_hash_map<absl::string_view, spdlog::level::level_enum>& changes) {
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

  return absl::OkStatus();
}

} // namespace Server
} // namespace Envoy
