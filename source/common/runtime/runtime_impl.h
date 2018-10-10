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
#include "envoy/stats/store.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/type/percent.pb.validate.h"

#include "common/common/assert.h"
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
  GAUGE  (num_keys)                                                                                \
  GAUGE  (admin_overrides_active)
// clang-format on

/**
 * Struct definition for all runtime stats. @see stats_macros.h
 */
struct RuntimeStats {
  ALL_RUNTIME_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Implementation of Snapshot whose source is the vector of layers passed to the constructor.
 */
class SnapshotImpl : public Snapshot,
                     public ThreadLocal::ThreadLocalObject,
                     Logger::Loggable<Logger::Id::runtime> {
public:
  SnapshotImpl(RandomGenerator& generator, RuntimeStats& stats,
               std::vector<OverrideLayerConstPtr>&& layers);

  // Runtime::Snapshot
  bool featureEnabled(const std::string& key, uint64_t default_value, uint64_t random_value,
                      uint64_t num_buckets) const override;
  bool featureEnabled(const std::string& key, uint64_t default_value) const override;
  bool featureEnabled(const std::string& key, uint64_t default_value,
                      uint64_t random_value) const override;
  bool featureEnabled(const std::string& key,
                      const envoy::type::FractionalPercent& default_value) const override;
  bool featureEnabled(const std::string& key, const envoy::type::FractionalPercent& default_value,
                      uint64_t random_value) const override;
  const std::string& get(const std::string& key) const override;
  uint64_t getInteger(const std::string& key, uint64_t default_value) const override;
  const std::vector<OverrideLayerConstPtr>& getLayers() const override;

  static Entry createEntry(const std::string& value);

private:
  static void resolveEntryType(Entry& entry) {
    if (parseEntryUintValue(entry)) {
      return;
    }
    parseEntryFractionalPercentValue(entry);
  }

  static bool parseEntryUintValue(Entry& entry);
  static void parseEntryFractionalPercentValue(Entry& entry);

  const std::vector<OverrideLayerConstPtr> layers_;
  EntryMap values_;
  RandomGenerator& generator_;
};

/**
 * Base implementation of OverrideLayer that by itself provides an empty values map.
 */
class OverrideLayerImpl : public Snapshot::OverrideLayer {
public:
  explicit OverrideLayerImpl(const std::string& name) : name_{name} {}
  const Snapshot::EntryMap& values() const override { return values_; }
  const std::string& name() const override { return name_; }

protected:
  Snapshot::EntryMap values_;
  const std::string name_;
};

/**
 * Extension of OverrideLayerImpl that maintains an in-memory set of values. These values can be
 * modified programmatically via mergeValues(). AdminLayer is so named because it can be accessed
 * and manipulated by Envoy's admin interface.
 */
class AdminLayer : public OverrideLayerImpl {
public:
  explicit AdminLayer(RuntimeStats& stats) : OverrideLayerImpl{"admin"}, stats_{stats} {}
  /**
   * Copy-constructible so that it can snapshotted.
   */
  AdminLayer(const AdminLayer& admin_layer) : AdminLayer{admin_layer.stats_} {
    values_ = admin_layer.values();
  }

  /**
   * Merge the provided values into our entry map. An empty value indicates that a key should be
   * removed from our map.
   */
  void mergeValues(const std::unordered_map<std::string, std::string>& values);

private:
  RuntimeStats& stats_;
};

/**
 * Extension of OverrideLayerImpl that loads values from the file system upon construction.
 */
class DiskLayer : public OverrideLayerImpl, Logger::Loggable<Logger::Id::runtime> {
public:
  DiskLayer(const std::string& name, const std::string& path, Api::OsSysCalls& os_sys_calls);

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

  void walkDirectory(const std::string& path, const std::string& prefix, uint32_t depth);

  const std::string path_;
  Api::OsSysCalls& os_sys_calls_;
  // Maximum recursion depth for walkDirectory().
  const uint32_t MaxWalkDepth = 16;
};

/**
 * Implementation of Loader that provides Snapshots of values added via mergeValues().
 * A single snapshot is shared among all threads and referenced by shared_ptr such that
 * a new runtime can be swapped in by the main thread while workers are still using the previous
 * version.
 */
class LoaderImpl : public Loader {
public:
  LoaderImpl(RandomGenerator& generator, Stats::Store& stats, ThreadLocal::SlotAllocator& tls);

  // Runtime::Loader
  Snapshot& snapshot() override;
  void mergeValues(const std::unordered_map<std::string, std::string>& values) override;

protected:
  // Identical the the public constructor but does not call loadSnapshot(). Subclasses must call
  // loadSnapshot() themselves to create the initial snapshot, since loadSnapshot calls the virtual
  // function createNewSnapshot() and is therefore unsuitable for use in a superclass constructor.
  struct DoNotLoadSnapshot {};
  LoaderImpl(DoNotLoadSnapshot /* unused */, RandomGenerator& generator, Stats::Store& stats,
             ThreadLocal::SlotAllocator& tls);

  // Create a new Snapshot
  virtual std::unique_ptr<SnapshotImpl> createNewSnapshot();
  // Load a new Snapshot into TLS
  void loadNewSnapshot();

  RandomGenerator& generator_;
  RuntimeStats stats_;
  AdminLayer admin_layer_;

private:
  RuntimeStats generateStats(Stats::Store& store);

  ThreadLocal::SlotPtr tls_;
};

/**
 * Extension of LoaderImpl that watches a symlink for swapping and loads a specified subdirectory
 * from disk. Values added via mergeValues() are secondary to those loaded from disk.
 */
class DiskBackedLoaderImpl : public LoaderImpl, Logger::Loggable<Logger::Id::runtime> {
public:
  DiskBackedLoaderImpl(Event::Dispatcher& dispatcher, ThreadLocal::SlotAllocator& tls,
                       const std::string& root_symlink_path, const std::string& subdir,
                       const std::string& override_dir, Stats::Store& store,
                       RandomGenerator& generator, Api::OsSysCallsPtr os_sys_calls);

private:
  std::unique_ptr<SnapshotImpl> createNewSnapshot() override;

  const Filesystem::WatcherPtr watcher_;
  const std::string root_path_;
  const std::string override_path_;
  const Api::OsSysCallsPtr os_sys_calls_;
};

} // namespace Runtime
} // namespace Envoy
