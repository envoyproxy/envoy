#include "source/common/common/fine_grain_logger.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <tuple>

#include "source/common/common/logger.h"

#include "absl/strings/str_join.h"

using spdlog::level::level_enum;

namespace Envoy {

/**
 * Implements a lock from BasicLockable, to avoid dependency problem of thread.h.
 */
class FineGrainLogBasicLockable : public Thread::BasicLockable {
public:
  // BasicLockable
  void lock() ABSL_EXCLUSIVE_LOCK_FUNCTION() override { mutex_.lock(); }
  bool tryLock() ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true) override { return mutex_.try_lock(); }
  void unlock() ABSL_UNLOCK_FUNCTION() override { mutex_.unlock(); }

private:
  absl::Mutex mutex_;
};

SpdLoggerSharedPtr FineGrainLogContext::getFineGrainLogEntry(absl::string_view key)
    ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_) {
  absl::ReaderMutexLock l(fine_grain_log_lock_);
  auto it = fine_grain_log_map_->find(key);
  if (it != fine_grain_log_map_->end()) {
    return it->second;
  }
  return nullptr;
}

SpdLoggerSharedPtr FineGrainLogContext::getFineGrainLogEntryForFlush(absl::string_view file,
                                                                     absl::string_view name)
    ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_) {
  if (name.empty()) {
    return getFineGrainLogEntry(file);
  }
  return getFineGrainLogEntry(absl::StrCat(file, ":", name));
}

SpdLoggerSharedPtr FineGrainLogContext::getFineGrainLogEntryForFlush(absl::string_view file,
                                                                     LoggerGroup group)
    ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_) {
  return getFineGrainLogEntry(absl::StrCat(file, ":", group.name_));
}
spdlog::level::level_enum FineGrainLogContext::getVerbosityDefaultLevel() const {
  absl::ReaderMutexLock l(fine_grain_log_lock_);
  return verbosity_default_level_;
}

spdlog::logger* FineGrainLogContext::initFineGrainLogger(absl::string_view file,
                                                         absl::string_view name,
                                                         std::atomic<spdlog::logger*>& logger)
    ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_) {
  std::string key(file);
  if (!name.empty()) {
    absl::StrAppend(&key, ":", name);
  }

  absl::WriterMutexLock l(fine_grain_log_lock_);
  auto it = fine_grain_log_map_->find(key);
  spdlog::logger* target;
  if (it == fine_grain_log_map_->end()) {
    target = createLogger(key);
  } else {
    target = it->second.get();
  }
  logger.store(target, std::memory_order_release);
  return target;
}

bool FineGrainLogContext::setFineGrainLogger(absl::string_view key, level_enum log_level)
    ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_) {
  absl::ReaderMutexLock l(fine_grain_log_lock_);
  auto it = fine_grain_log_map_->find(key);
  if (it != fine_grain_log_map_->end()) {
    it->second->set_level(log_level);
    return true;
  }
  return false;
}

void FineGrainLogContext::setDefaultFineGrainLogLevelFormat(spdlog::level::level_enum level,
                                                            const std::string& format)
    ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_) {
  {
    absl::WriterMutexLock wl(fine_grain_log_lock_);
    verbosity_default_level_ = level;
  }

  absl::ReaderMutexLock rl(fine_grain_log_lock_);
  for (const auto& [key, logger] : *fine_grain_log_map_) {
    logger->set_level(getLogLevel(key));
    Logger::Utility::setLogFormatForLogger(*logger, format);
  }
}

std::string FineGrainLogContext::listFineGrainLoggers() ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_) {
  absl::ReaderMutexLock l(fine_grain_log_lock_);
  std::vector<std::string> lines;
  for (const auto& log_pair : *fine_grain_log_map_) {
    const auto [file, logger_name] = parseKey(log_pair.first);
    if (!logger_name.empty()) {
      continue;
    }
    auto level_str_view = spdlog::level::to_string_view(log_pair.second->level());
    lines.push_back(absl::StrCat("  ", file, ": ",
                                 absl::string_view(level_str_view.data(), level_str_view.size())));
  }
  return absl::StrJoin(lines, "\n");
}

void FineGrainLogContext::setAllFineGrainLoggers(spdlog::level::level_enum level)
    ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_) {
  absl::ReaderMutexLock l(fine_grain_log_lock_);
  verbosity_update_info_.clear();
  for (const auto& it : *fine_grain_log_map_) {
    it.second->set_level(level);
  }
}

FineGrainLogLevelMap FineGrainLogContext::getAllFineGrainLogLevelsForTest()
    ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_) {
  FineGrainLogLevelMap log_levels;
  absl::ReaderMutexLock l(fine_grain_log_lock_);
  for (const auto& it : *fine_grain_log_map_) {
    log_levels[it.first] = it.second->level();
  }
  return log_levels;
}

bool FineGrainLogContext::safeFileNameMatch(absl::string_view pattern, absl::string_view str) {
  while (true) {
    if (pattern.empty()) {
      // `pattern` is exhausted; succeed if all of `str` was consumed matching it.
      return str.empty();
    }
    if (str.empty()) {
      // `str` is exhausted; succeed if `pattern` is empty or all '*'s.
      return pattern.find_first_not_of('*') == pattern.npos;
    }
    if (pattern.front() == '*') {
      pattern.remove_prefix(1);
      if (pattern.empty()) {
        return true;
      }
      do {
        if (safeFileNameMatch(pattern, str)) {
          return true;
        }
        str.remove_prefix(1);
      } while (!str.empty());
      return false;
    }
    if (pattern.front() == '?' || pattern.front() == str.front()) {
      pattern.remove_prefix(1);
      str.remove_prefix(1);
      continue;
    }
    return false;
  }
}

void FineGrainLogContext::initSink() {
  spdlog::sink_ptr sink = Logger::Registry::getSink();
  Logger::DelegatingLogSinkSharedPtr sp = std::static_pointer_cast<Logger::DelegatingLogSink>(sink);
  if (!sp->hasLock()) {
    static FineGrainLogBasicLockable tlock;
    sp->setLock(tlock);
    sp->setShouldEscape(false);
  }
}

spdlog::logger* FineGrainLogContext::createLogger(const std::string& key)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(fine_grain_log_lock_) {
  SpdLoggerSharedPtr new_logger =
      std::make_shared<spdlog::logger>(key, Logger::Registry::getSink());
  if (!Logger::Registry::getSink()->hasLock()) { // occurs in benchmark test
    initSink();
  }

  new_logger->set_level(getLogLevel(key));
  Logger::Utility::setLogFormatForLogger(*new_logger, Logger::Context::getFineGrainLogFormat());
  new_logger->flush_on(level_enum::critical);
  fine_grain_log_map_->insert(std::make_pair(key, new_logger));
  return new_logger.get();
}

void FineGrainLogContext::updateVerbosityDefaultLevel(level_enum level) {
  {
    absl::WriterMutexLock wl(fine_grain_log_lock_);
    verbosity_default_level_ = level;
  }

  setAllFineGrainLoggers(level);
}

void FineGrainLogContext::updateVerbositySetting(const std::vector<VerbosityUpdate>& updates) {
  absl::WriterMutexLock ul(fine_grain_log_lock_);
  verbosity_update_info_.clear();
  for (const auto& update : updates) {
    if (update.level < kLogLevelMin || update.level > kLogLevelMax) {
      printf("The log level: %d for pattern: %s is out of scope, and it should be in [0, 6]. "
             "Skipping.",
             update.level, std::string(update.pattern).c_str());
      continue;
    }
    appendVerbosityLogUpdate(update.pattern, static_cast<level_enum>(update.level),
                             update.match_group_only);
  }

  for (const auto& [key, logger] : *fine_grain_log_map_) {
    logger->set_level(getLogLevel(key));
  }
}

void FineGrainLogContext::appendVerbosityLogUpdate(absl::string_view update_pattern,
                                                   level_enum log_level, bool match_group_only) {
  for (const auto& info : verbosity_update_info_) {
    if (info.match_group_only == match_group_only &&
        safeFileNameMatch(info.update_pattern, update_pattern)) {
      // This is a memory optimization to avoid storing patterns that will never
      // match due to exit early semantics.
      return;
    }
  }
  bool update_is_path = update_pattern.find('/') != update_pattern.npos;
  verbosity_update_info_.emplace_back(std::string(update_pattern), update_is_path, match_group_only,
                                      log_level);
}
level_enum FineGrainLogContext::getLogLevel(absl::string_view key) const {
  if (verbosity_update_info_.empty()) {
    return verbosity_default_level_;
  }

  const auto [file, logger_name] = parseKey(key);

  // Get basename for file.
  absl::string_view file_basename = file;
  {
    const size_t sep = file_basename.rfind('/');
    if (sep != file_basename.npos) {
      file_basename.remove_prefix(sep + 1);
    }
  }

  absl::string_view stem_basename = file_basename;
  {
    const size_t sep = stem_basename.find('.');
    if (sep != stem_basename.npos) {
      stem_basename.remove_suffix(stem_basename.size() - sep);
    }
  }
  for (const auto& info : verbosity_update_info_) {
    if (info.match_group_only) {
      if (!logger_name.empty() && info.update_pattern == logger_name) {
        return info.log_level;
      }
      continue;
    }

    if (info.update_is_path) {
      // If there are any slashes in the pattern, try to match the full path name.
      if (safeFileNameMatch(info.update_pattern, file)) {
        return info.log_level;
      }
    } else {
      if (safeFileNameMatch(info.update_pattern, stem_basename)) {
        return info.log_level;
      }
      if (!logger_name.empty() && info.update_pattern == logger_name) {
        return info.log_level;
      }
    }
  }

  return verbosity_default_level_;
}

std::pair<absl::string_view, absl::string_view>
FineGrainLogContext::parseKey(absl::string_view key) {
  absl::string_view file = key;
  absl::string_view name;
  size_t colon = key.rfind(':');
#ifdef _WIN32
  // On Windows, a colon at index 1 is likely a drive letter (e.g., C:\path).
  if (colon == 1 && key.size() > 1 &&
      ((key[0] >= 'a' && key[0] <= 'z') || (key[0] >= 'A' && key[0] <= 'Z'))) {
    colon = absl::string_view::npos;
  }
#endif
  if (colon != absl::string_view::npos) {
    name = key.substr(colon + 1);
    file = key.substr(0, colon);
  }
  return {file, name};
}

FineGrainLogContext& getFineGrainLogContext() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(FineGrainLogContext);
}

} // namespace Envoy
