#include "source/common/common/fancy_logger.h"

#include <atomic>
#include <memory>

#include "source/common/common/logger.h"

using spdlog::level::level_enum;

namespace Envoy {

/**
 * Implements a lock from BasicLockable, to avoid dependency problem of thread.h.
 */
class FancyBasicLockable : public Thread::BasicLockable {
public:
  // BasicLockable
  void lock() ABSL_EXCLUSIVE_LOCK_FUNCTION() override { mutex_.Lock(); }
  bool tryLock() ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true) override { return mutex_.TryLock(); }
  void unlock() ABSL_UNLOCK_FUNCTION() override { mutex_.Unlock(); }

private:
  absl::Mutex mutex_;
};

SpdLoggerSharedPtr FancyContext::getFancyLogEntry(std::string key)
    ABSL_LOCKS_EXCLUDED(fancy_log_lock_) {
  absl::ReaderMutexLock l(&fancy_log_lock_);
  auto it = fancy_log_map_->find(key);
  if (it != fancy_log_map_->end()) {
    return it->second;
  }
  return nullptr;
}

void FancyContext::initFancyLogger(std::string key, std::atomic<spdlog::logger*>& logger)
    ABSL_LOCKS_EXCLUDED(fancy_log_lock_) {
  absl::WriterMutexLock l(&fancy_log_lock_);
  auto it = fancy_log_map_->find(key);
  spdlog::logger* target;
  if (it == fancy_log_map_->end()) {
    target = createLogger(key);
  } else {
    target = it->second.get();
  }
  logger.store(target);
}

bool FancyContext::setFancyLogger(std::string key, level_enum log_level)
    ABSL_LOCKS_EXCLUDED(fancy_log_lock_) {
  absl::ReaderMutexLock l(&fancy_log_lock_);
  auto it = fancy_log_map_->find(key);
  if (it != fancy_log_map_->end()) {
    it->second->set_level(log_level);
    return true;
  }
  return false;
}

void FancyContext::setDefaultFancyLevelFormat(spdlog::level::level_enum level, std::string format)
    ABSL_LOCKS_EXCLUDED(fancy_log_lock_) {
  if (level == Logger::Context::getFancyDefaultLevel() &&
      format == Logger::Context::getFancyLogFormat()) {
    return;
  }
  absl::ReaderMutexLock l(&fancy_log_lock_);
  for (const auto& it : *fancy_log_map_) {
    if (it.second->level() == Logger::Context::getFancyDefaultLevel()) {
      // if logger is default level now
      it.second->set_level(level);
    }
    it.second->set_pattern(format);
  }
}

std::string FancyContext::listFancyLoggers() ABSL_LOCKS_EXCLUDED(fancy_log_lock_) {
  std::string info = "";
  absl::ReaderMutexLock l(&fancy_log_lock_);
  for (const auto& it : *fancy_log_map_) {
    info += fmt::format("   {}: {}\n", it.first, static_cast<int>(it.second->level()));
  }
  return info;
}

void FancyContext::setAllFancyLoggers(spdlog::level::level_enum level)
    ABSL_LOCKS_EXCLUDED(fancy_log_lock_) {
  absl::ReaderMutexLock l(&fancy_log_lock_);
  for (const auto& it : *fancy_log_map_) {
    it.second->set_level(level);
  }
}

FancyLogLevelMap FancyContext::getAllFancyLogLevelsForTest() ABSL_LOCKS_EXCLUDED(fancy_log_lock_) {
  FancyLogLevelMap log_levels;
  absl::ReaderMutexLock l(&fancy_log_lock_);
  for (const auto& it : *fancy_log_map_) {
    log_levels[it.first] = it.second->level();
  }
  return log_levels;
}

bool FancyContext::safeFileNameMatch(absl::string_view pattern, absl::string_view str) {
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

void FancyContext::initSink() {
  spdlog::sink_ptr sink = Logger::Registry::getSink();
  Logger::DelegatingLogSinkSharedPtr sp = std::static_pointer_cast<Logger::DelegatingLogSink>(sink);
  if (!sp->hasLock()) {
    static FancyBasicLockable tlock;
    sp->setLock(tlock);
    sp->setShouldEscape(false);
  }
}

spdlog::logger* FancyContext::createLogger(const std::string& key)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(fancy_log_lock_) {
  SpdLoggerSharedPtr new_logger =
      std::make_shared<spdlog::logger>(key, Logger::Registry::getSink());
  if (!Logger::Registry::getSink()->hasLock()) { // occurs in benchmark test
    initSink();
  }

  new_logger->set_level(getLogLevel(key));
  new_logger->set_pattern(Logger::Context::getFancyLogFormat());
  new_logger->flush_on(level_enum::critical);
  fancy_log_map_->insert(std::make_pair(key, new_logger));
  return new_logger.get();
}

void FancyContext::updateVerbositySetting(
    const std::vector<std::pair<absl::string_view, int>>& updates) {
  absl::WriterMutexLock ul(&fancy_log_lock_);
  log_update_info_.clear();
  for (const auto& [glob, level] : updates) {
    if (level < kLogLevelMin || level > kLogLevelMax) {
      printf(
          "The log level: %d for glob: %s is out of scope, and it should be in [0, 6]. Skipping.",
          level, std::string(glob).c_str());
      continue;
    }
    appendVerbosityLogUpdate(glob, static_cast<level_enum>(level));
  }

  for (auto& [key, logger] : *fancy_log_map_) {
    logger->set_level(getLogLevel(key));
  }
}

void FancyContext::appendVerbosityLogUpdate(absl::string_view update_pattern,
                                            level_enum log_level) {
  for (const auto& info : log_update_info_) {
    if (safeFileNameMatch(info.update_pattern, update_pattern)) {
      // This is a memory optimization to avoid storing patterns that will never
      // match due to exit early semantics.
      return;
    }
  }
  bool update_is_path = update_pattern.find('/') != update_pattern.npos;
  log_update_info_.emplace_back(std::string(update_pattern), update_is_path, log_level);
}

level_enum FancyContext::getLogLevel(absl::string_view file) const {
  if (log_update_info_.empty()) {
    return Logger::Context::getFancyDefaultLevel();
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
  for (const auto& info : log_update_info_) {
    if (info.update_is_path) {
      // If there are any slashes in the pattern, try to match the full path name.
      if (safeFileNameMatch(info.update_pattern, stem)) {
        return info.log_level;
      }
    } else if (safeFileNameMatch(info.update_pattern, stem_basename)) {
      return info.log_level;
    }
  }

  return Logger::Context::getFancyDefaultLevel();
}

FancyContext& getFancyContext() { MUTABLE_CONSTRUCT_ON_FIRST_USE(FancyContext); }

} // namespace Envoy
