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
 * Implementation of Snapshot whose source is the values map provided to its constructor.
 * Used by LoaderImpl.
 */
class SnapshotImpl : public Snapshot, public ThreadLocal::ThreadLocalObject {
public:
  SnapshotImpl(RandomGenerator& generator,
               const std::unordered_map<std::string, std::string>& values);

  // Runtime::Snapshot
  bool featureEnabled(const std::string& key, uint64_t default_value, uint64_t random_value,
                      uint64_t num_buckets) const override;
  bool featureEnabled(const std::string& key, uint64_t default_value) const override;
  bool featureEnabled(const std::string& key, uint64_t default_value,
                      uint64_t random_value) const override;
  const std::string& get(const std::string& key) const override;
  uint64_t getInteger(const std::string& key, uint64_t default_value) const override;
  const std::unordered_map<std::string, const Snapshot::Entry>& getAll() const override;

protected:
  explicit SnapshotImpl(RandomGenerator& generator);

  static void tryConvertToInteger(Snapshot::Entry& entry);
  void mergeValues(const std::unordered_map<std::string, std::string>& values);

  std::unordered_map<std::string, const Snapshot::Entry> values_;

private:
  RandomGenerator& generator_;
};

/**
 * Extension of SnapshotImpl that uses the disk as its primary source of values and SnapshotImpl's
 * in-memory values as overrides. Used by DiskBackedLoaderImpl.
 */
class DiskBackedSnapshotImpl : public SnapshotImpl, Logger::Loggable<Logger::Id::runtime> {
public:
  DiskBackedSnapshotImpl(RandomGenerator& generator,
                         const std::unordered_map<std::string, std::string>& additional_overrides,
                         const std::string& root_path, const std::string& override_path,
                         RuntimeStats& stats, Api::OsSysCalls& os_sys_calls);

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

  Api::OsSysCalls& os_sys_calls_;
};

/**
 * Implementation of Loader that provides Snapshots of values added via mergeValues().
 * A single snapshot is shared among all threads and referenced by shared_ptr such that
 * a new runtime can be swapped in by the main thread while workers are still using the previous
 * version.
 */
class LoaderImpl : public Loader {
public:
  LoaderImpl(RandomGenerator& generator, ThreadLocal::SlotAllocator& tls);

  // Runtime::Loader
  Snapshot& snapshot() override;
  void mergeValues(const std::unordered_map<std::string, std::string>& values) override;

protected:
  // Identical the the public constructor but does not call loadSnapshot(). Subclasses must call
  // loadSnapshot() themselves to create the initial snapshot, since loadSnapshot calls the virtual
  // function createNewSnapshot() and is therefore unsuitable for use in a superclass constructor.
  struct DoNotLoadSnapshot {};
  LoaderImpl(DoNotLoadSnapshot /* unused */, RandomGenerator& generator,
             ThreadLocal::SlotAllocator& tls);

  // Create a new Snapshot
  virtual std::unique_ptr<SnapshotImpl> createNewSnapshot();
  // Load a new Snapshot into TLS
  void loadNewSnapshot();

  RandomGenerator& generator_;
  std::unordered_map<std::string, std::string> values_;

private:
  ThreadLocal::SlotPtr tls_;
};

/**
 * Extension of LoaderImpl that watches a symlink for swapping and loads a specified subdirectory
 * from disk. Values added via mergeValues() are secondary to those loaded from disk.
 */
class DiskBackedLoaderImpl : public LoaderImpl {
public:
  DiskBackedLoaderImpl(Event::Dispatcher& dispatcher, ThreadLocal::SlotAllocator& tls,
                       const std::string& root_symlink_path, const std::string& subdir,
                       const std::string& override_dir, Stats::Store& store,
                       RandomGenerator& generator, Api::OsSysCallsPtr os_sys_calls);

private:
  std::unique_ptr<SnapshotImpl> createNewSnapshot() override;
  RuntimeStats generateStats(Stats::Store& store);

  Filesystem::WatcherPtr watcher_;
  std::string root_path_;
  std::string override_path_;
  RuntimeStats stats_;
  Api::OsSysCallsPtr os_sys_calls_;
};

} // namespace Runtime
} // namespace Envoy
