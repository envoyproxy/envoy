#include "source/common/common/fine_grain_logger.h"

#include <atomic>
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
  void lock() ABSL_EXCLUSIVE_LOCK_FUNCTION() override { mutex_.Lock(); }
  bool tryLock() ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true) override { return mutex_.TryLock(); }
  void unlock() ABSL_UNLOCK_FUNCTION() override { mutex_.Unlock(); }

private:
  absl::Mutex mutex_;
};

SpdLoggerSharedPtr FineGrainLogContext::getFineGrainLogEntry(absl::string_view key)
    ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_) {
  absl::ReaderMutexLock l(&fine_grain_log_lock_);
  auto it = fine_grain_log_map_->find(key);
  if (it != fine_grain_log_map_->end()) {
    return it->second;
  }
  return nullptr;
}

spdlog::level::level_enum FineGrainLogContext::getVerbosityDefaultLevel() const {
  absl::ReaderMutexLock l(&fine_grain_log_lock_);
  return verbosity_default_level_;
}

void FineGrainLogContext::initFineGrainLogger(const std::string& key,
                                              std::atomic<spdlog::logger*>& logger)
    ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_) {
  absl::WriterMutexLock l(&fine_grain_log_lock_);
  auto it = fine_grain_log_map_->find(key);
  spdlog::logger* target;
  if (it == fine_grain_log_map_->end()) {
    target = createLogger(key);
  } else {
    target = it->second.get();
  }
  logger.store(target);
}

bool FineGrainLogContext::setFineGrainLogger(absl::string_view key, level_enum log_level)
    ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_) {
  absl::ReaderMutexLock l(&fine_grain_log_lock_);
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
    absl::WriterMutexLock wl(&fine_grain_log_lock_);
    verbosity_default_level_ = level;
  }

  absl::ReaderMutexLock rl(&fine_grain_log_lock_);
  for (const auto& [key, logger] : *fine_grain_log_map_) {
    logger->set_level(getLogLevel(key));
    Logger::Utility::setLogFormatForLogger(*logger, format);
  }
}

std::string FineGrainLogContext::listFineGrainLoggers() ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_) {
  absl::ReaderMutexLock l(&fine_grain_log_lock_);
  std::string info =
      absl::StrJoin(*fine_grain_log_map_, "\n", [](std::string* out, const auto& log_pair) {
        absl::StrAppend(out, "  ", log_pair.first, ": ", log_pair.second->level());
      });
  return info;
}

void FineGrainLogContext::setAllFineGrainLoggers(spdlog::level::level_enum level)
    ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_) {
  absl::ReaderMutexLock l(&fine_grain_log_lock_);
  if (verbosity_update_info_.empty()) {
    for (const auto& it : *fine_grain_log_map_) {
      it.second->set_level(level);
    }
  } else {
    for (const auto& [key, logger] : *fine_grain_log_map_) {
      logger->set_level(getLogLevel(key));
    }
  }
}

FineGrainLogLevelMap FineGrainLogContext::getAllFineGrainLogLevelsForTest()
    ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_) {
  FineGrainLogLevelMap log_levels;
  absl::ReaderMutexLock l(&fine_grain_log_lock_);
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
    absl::WriterMutexLock wl(&fine_grain_log_lock_);
    if (level == verbosity_default_level_) {
      return;
    }
    verbosity_default_level_ = level;
  }

  setAllFineGrainLoggers(level);
}

void FineGrainLogContext::updateVerbositySetting(
    const std::vector<std::pair<absl::string_view, int>>& updates) {
  absl::WriterMutexLock ul(&fine_grain_log_lock_);
  verbosity_update_info_.clear();
  for (const auto& [glob, level] : updates) {
    if (level < kLogLevelMin || level > kLogLevelMax) {
      printf(
          "The log level: %d for glob: %s is out of scope, and it should be in [0, 6]. Skipping.",
          level, std::string(glob).c_str());
      continue;
    }
    appendVerbosityLogUpdate(glob, static_cast<level_enum>(level));
  }

  for (const auto& [key, logger] : *fine_grain_log_map_) {
    logger->set_level(getLogLevel(key));
  }
}

void FineGrainLogContext::appendVerbosityLogUpdate(absl::string_view update_pattern,
                                                   level_enum log_level) {
  for (const auto& info : verbosity_update_info_) {
    if (safeFileNameMatch(info.update_pattern, update_pattern)) {
      // This is a memory optimization to avoid storing patterns that will never
      // match due to exit early semantics.
      return;
    }
  }
  bool update_is_path = update_pattern.find('/') != update_pattern.npos;
  verbosity_update_info_.emplace_back(std::string(update_pattern), update_is_path, log_level);
}

level_enum FineGrainLogContext::getLogLevel(absl::string_view file) const {
  if (verbosity_update_info_.empty()) {
    return verbosity_default_level_;
  }

  // Get basename for file.
  absl::string_view basename = file;
  {
    const size_t sep = basename.rfind('/');
    if (sep != basename.npos) {
      basename.remove_prefix(sep + 1);
    }
  }

  absl::string_view stem = file, stem_basename = basename;
  {
    const size_t sep = stem_basename.find('.');
    if (sep != stem_basename.npos) {
      stem.remove_suffix(stem_basename.size() - sep);
      stem_basename.remove_suffix(stem_basename.size() - sep);
    }
  }
  for (const auto& info : verbosity_update_info_) {
    if (info.update_is_path) {
      // If there are any slashes in the pattern, try to match the full path name.
      if (safeFileNameMatch(info.update_pattern, stem)) {
        return info.log_level;
      }
    } else if (safeFileNameMatch(info.update_pattern, stem_basename)) {
      return info.log_level;
    }
  }

  return verbosity_default_level_;
}

FineGrainLogContext& getFineGrainLogContext() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(FineGrainLogContext);
}

} // namespace Envoy
