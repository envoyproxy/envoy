#include "source/server/admin/logs_handler.h"

#include <string>

#include "source/common/common/fine_grain_logger.h"
#include "source/common/common/logger.h"
#include "source/server/admin/utils.h"

#include "absl/strings/str_split.h"

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

const absl::flat_hash_map<absl::string_view, std::vector<absl::string_view>>&
LogsHandler::getLoggerGroups() {
  // Predefined logger groups for common subsystems.
  static const absl::flat_hash_map<absl::string_view, std::vector<absl::string_view>> groups({
    {"http", {"source/common/http/*", "source/common/http/http1/*", "source/common/http/http2/*", "source/common/http/http3/*"}},
    {"router", {"source/common/router/*"}},
    {"network", {"source/common/network/*"}},
    {"upstream", {"source/common/upstream/*"}},
    {"connection", {"source/common/tcp/*", "source/common/network/connection_impl.cc", "source/common/network/filter_manager_impl.cc"}},
    {"admin", {"source/server/admin/*"}},
    {"config", {"source/common/config/*", "source/server/config_validation/*"}},
    {"grpc", {"source/common/grpc/*"}},
    {"filter", {"source/common/filter/*", "source/extensions/filters/*"}},
    {"listener", {"source/server/listener*", "source/common/listener_manager/*"}},
  });
  return groups;
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
    response.add("groups: ");
    const auto& groups = getLoggerGroups();
    for (const auto& group : groups) {
      response.add(fmt::format("{} ", group.first));
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
  // "level", "paths", and "group" will be set to the empty string when this is invoked
  // from HTML without setting them, so clean out empty values.
  auto level = params.getFirstValue("level");
  if (level.has_value() && level.value().empty()) {
    params.remove("level");
    level = std::nullopt;
  }
  auto paths = params.getFirstValue("paths");
  if (paths.has_value() && paths.value().empty()) {
    params.remove("paths");
    paths = std::nullopt;
  }
  auto group = params.getFirstValue("group");
  if (group.has_value() && group.value().empty()) {
    params.remove("group");
    group = std::nullopt;
  }

  if (params.data().empty()) {
    return absl::OkStatus();
  }

  if (params.data().size() != 1) {
    return absl::InvalidArgumentError("invalid number of parameters");
  }

  if (level.has_value()) {
    // Change all log levels.
    const absl::StatusOr<spdlog::level::level_enum> level_to_use = parseLogLevel(level.value());
    if (!level_to_use.ok()) {
      return level_to_use.status();
    }

    Logger::Context::changeAllLogLevels(*level_to_use);
    return absl::OkStatus();
  }

  // Build a map of name:level pairs, a few allocations is ok here since it's
  // not common to call this function at a high rate.
  absl::flat_hash_map<absl::string_view, spdlog::level::level_enum> name_levels;
  std::vector<std::pair<absl::string_view, int>> glob_levels;
  const bool use_fine_grain_logger = Logger::Context::useFineGrainLogger();

  if (group.has_value()) {
    // Group parameter requires fine-grain logging to be enabled
    if (!use_fine_grain_logger) {
      return absl::InvalidArgumentError("group parameter requires fine-grain logging to be enabled");
    }

    // Handle group parameter: group=<group_name>:<level>
    // Split on colon and trim whitespace from each part to allow flexible input like "http : debug"
    const std::pair<absl::string_view, absl::string_view> group_level_pair =
        absl::StrSplit(group.value(), absl::MaxSplits(':', 1));
    absl::string_view group_name = absl::StripAsciiWhitespace(group_level_pair.first);
    absl::string_view group_level = absl::StripAsciiWhitespace(group_level_pair.second);
    if (group_name.empty() || group_level.empty()) {
      return absl::InvalidArgumentError("empty group name or empty log level");
    }

    const absl::StatusOr<spdlog::level::level_enum> level_to_use = parseLogLevel(group_level);
    if (!level_to_use.ok()) {
      return level_to_use.status();
    }

    const auto& groups = getLoggerGroups();
    auto group_it = groups.find(group_name);
    if (group_it == groups.end()) {
      return absl::InvalidArgumentError(fmt::format("unknown group name: {}", group_name));
    }

    // Apply the log level to all file patterns in the group
    for (const auto& pattern : group_it->second) {
      glob_levels.emplace_back(pattern, *level_to_use);
    }

    ENVOY_LOG(info, "applying fine-grain log levels for group='{}' with {} pattern(s) at level '{}'",
              group_name, group_it->second.size(), spdlog::level::level_string_views[*level_to_use]);
    getFineGrainLogContext().updateVerbositySetting(glob_levels);
    return absl::OkStatus();
  }

  if (paths.has_value()) {
    // Bulk change log level by name:level pairs, separated by comma.
    std::vector<absl::string_view> pairs =
        absl::StrSplit(paths.value(), ',', absl::SkipWhitespace());
    for (const auto& name_level : pairs) {
      const std::pair<absl::string_view, absl::string_view> name_level_pair =
          absl::StrSplit(name_level, absl::MaxSplits(':', 1), absl::SkipWhitespace());
      auto [name, level] = name_level_pair;
      if (name.empty() || level.empty()) {
        return absl::InvalidArgumentError("empty logger name or empty logger level");
      }

      const absl::StatusOr<spdlog::level::level_enum> level_to_use = parseLogLevel(level);
      if (!level_to_use.ok()) {
        return level_to_use.status();
      }

      if (use_fine_grain_logger) {
        ENVOY_LOG(info, "adding fine-grain log update, glob='{}' level='{}'", name,
                  spdlog::level::level_string_views[*level_to_use]);
        glob_levels.emplace_back(name, *level_to_use);
      } else {
        name_levels[name] = *level_to_use;
      }
    }
  } else {
    // The HTML admin interface will always populate "level" and "paths" though
    // they may be empty. There's a legacy non-HTML-accessible mechanism to
    // set a single logger to a level, which we'll handle now. In this scenario,
    // "level" and "paths" will not be populated.
    if (params.data().size() != 1) {
      return absl::InvalidArgumentError("invalid number of parameters");
    }

    // Change particular log level by name.
    const auto it = params.data().begin();
    const std::string& key = it->first;
    const std::string& value = it->second[0];

    const absl::StatusOr<spdlog::level::level_enum> level_to_use = parseLogLevel(value);
    if (!level_to_use.ok()) {
      return level_to_use.status();
    }

    if (use_fine_grain_logger) {
      ENVOY_LOG(info, "adding fine-grain log update, glob='{}' level='{}'", key,
                spdlog::level::level_string_views[*level_to_use]);
      glob_levels.emplace_back(key, *level_to_use);
    } else {
      name_levels[key] = *level_to_use;
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
