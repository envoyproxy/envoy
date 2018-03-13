#pragma once

#include <dirent.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "envoy/api/os_sys_calls.h"
#include "envoy/common/exception.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/empty_string.h"
#include "common/common/logger.h"
#include "common/common/thread.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Runtime {

/**
 * Implementation of RandomGenerator that uses per-thread RANLUX generators seeded with current
 * time.
 */
class RandomGeneratorImpl : public RandomGenerator {
public:
  // Runtime::RandomGenerator
  uint64_t random() override;
  std::string uuid() override;

  static const size_t UUID_LENGTH;
};

/**
 * All runtime stats. @see stats_macros.h
 */
// clang-format off
#define ALL_RUNTIME_STATS(COUNTER, GAUGE)                                                          \
  COUNTER(load_error)                                                                              \
  COUNTER(override_dir_not_exists)                                                                 \
  COUNTER(override_dir_exists)                                                                     \
  COUNTER(load_success)                                                                            \
  GAUGE  (num_keys)
// clang-format on

/**
 * Struct definition for all runtime stats. @see stats_macros.h
 */
struct RuntimeStats {
  ALL_RUNTIME_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Implementation of Snapshot that reads from disk.
 */
class SnapshotImpl : public Snapshot,
                     public ThreadLocal::ThreadLocalObject,
                     Logger::Loggable<Logger::Id::runtime> {
public:
  SnapshotImpl(const std::string& root_path, const std::string& override_path, RuntimeStats& stats,
               RandomGenerator& generator, Api::OsSysCalls& os_sys_calls);

  // Runtime::Snapshot
  bool featureEnabled(const std::string& key, uint64_t default_value, uint64_t random_value,
                      uint64_t num_buckets) const override {
    return random_value % num_buckets < std::min(getInteger(key, default_value), num_buckets);
  }

  bool featureEnabled(const std::string& key, uint64_t default_value) const override {
    // Avoid PNRG if we know we don't need it.
    uint64_t cutoff = std::min(getInteger(key, default_value), static_cast<uint64_t>(100));
    if (cutoff == 0) {
      return false;
    } else if (cutoff == 100) {
      return true;
    } else {
      return generator_.random() % 100 < cutoff;
    }
  }

  bool featureEnabled(const std::string& key, uint64_t default_value,
                      uint64_t random_value) const override {
    return featureEnabled(key, default_value, random_value, 100);
  }

  const std::string& get(const std::string& key) const override;
  uint64_t getInteger(const std::string&, uint64_t default_value) const override;
  const std::unordered_map<std::string, const Snapshot::Entry>& getAll() const override;

private:
  struct Directory {
    Directory(const std::string& path) {
      dir_ = opendir(path.c_str());
      if (!dir_) {
        throw EnvoyException(fmt::format("unable to open directory: {}", path));
      }
    }

    ~Directory() { closedir(dir_); }

    DIR* dir_;
  };

  void walkDirectory(const std::string& path, const std::string& prefix);

  std::unordered_map<std::string, const Entry> values_;
  RandomGenerator& generator_;
  Api::OsSysCalls& os_sys_calls_;
};

/**
 * Implementation of Loader that watches a symlink for swapping and loads a specified subdirectory
 * from disk. A single snapshot is shared among all threads and referenced by shared_ptr such that
 * a new runtime can be swapped in by the main thread while workers are still using the previous
 * version.
 */
class LoaderImpl : public Loader {
public:
  LoaderImpl(Event::Dispatcher& dispatcher, ThreadLocal::SlotAllocator& tls,
             const std::string& root_symlink_path, const std::string& subdir,
             const std::string& override_dir, Stats::Store& store, RandomGenerator& generator,
             Api::OsSysCallsPtr os_sys_calls);

  // Runtime::Loader
  Snapshot& snapshot() override;

private:
  RuntimeStats generateStats(Stats::Store& store);
  void onSymlinkSwap();

  Filesystem::WatcherPtr watcher_;
  ThreadLocal::SlotPtr tls_;
  RandomGenerator& generator_;
  std::string root_path_;
  std::string override_path_;
  std::shared_ptr<SnapshotImpl> current_snapshot_;
  RuntimeStats stats_;
  Api::OsSysCallsPtr os_sys_calls_;
};

/**
 * Null implementation of runtime if another runtime source is not configured.
 */
class NullLoaderImpl : public Loader {
public:
  NullLoaderImpl(RandomGenerator& generator) : snapshot_(generator) {}

  // Runtime::Loader
  Snapshot& snapshot() override { return snapshot_; }

private:
  struct NullSnapshotImpl : public Snapshot {
    NullSnapshotImpl(RandomGenerator& generator) : generator_(generator) {}

    // Runtime::Snapshot
    bool featureEnabled(const std::string&, uint64_t default_value, uint64_t random_value,
                        uint64_t num_buckets) const override {
      return random_value % num_buckets < std::min(default_value, num_buckets);
    }

    bool featureEnabled(const std::string& key, uint64_t default_value) const override {
      if (default_value == 0) {
        return false;
      } else if (default_value == 100) {
        return true;
      } else {
        return featureEnabled(key, default_value, generator_.random());
      }
    }

    bool featureEnabled(const std::string& key, uint64_t default_value,
                        uint64_t random_value) const override {
      return featureEnabled(key, default_value, random_value, 100);
    }

    const std::string& get(const std::string&) const override { return EMPTY_STRING; }

    uint64_t getInteger(const std::string&, uint64_t default_value) const override {
      return default_value;
    }

    const std::unordered_map<std::string, const Snapshot::Entry>& getAll() const override {
      return values_;
    }

    RandomGenerator& generator_;
    std::unordered_map<std::string, const Snapshot::Entry> values_;
  };

  NullSnapshotImpl snapshot_;
};

} // namespace Runtime
} // namespace Envoy
