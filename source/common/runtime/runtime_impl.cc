#include "common/runtime/runtime_impl.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <random>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/filesystem/filesystem_impl.h"

#include "fmt/format.h"
#include "openssl/rand.h"

namespace Envoy {
namespace Runtime {

const size_t RandomGeneratorImpl::UUID_LENGTH = 36;

std::string RandomGeneratorImpl::uuid() {
  static thread_local uint8_t buffered[2048];
  static thread_local size_t buffered_idx = sizeof(buffered);

  // Prefetch 2048 bytes of randomness. buffered_idx is initialized to sizeof(buffered),
  // i.e. out-of-range value, so the buffer will be filled with randomness on the first
  // call to this function.
  if (buffered_idx >= sizeof(buffered)) {
    int rc = RAND_bytes(buffered, sizeof(buffered));
    ASSERT(rc == 1);
    UNREFERENCED_PARAMETER(rc);
    buffered_idx = 0;
  }

  // Consume 16 bytes from the buffer.
  ASSERT(buffered_idx + 16 <= sizeof(buffered));
  uint8_t* rand = &buffered[buffered_idx];
  buffered_idx += 16;

  // Create UUID from Truly Random or Pseudo-Random Numbers.
  // See: https://tools.ietf.org/html/rfc4122#section-4.4
  rand[6] = (rand[6] & 0x0f) | 0x40; // UUID version 4 (random)
  rand[8] = (rand[8] & 0x3f) | 0x80; // UUID variant 1 (RFC4122)

  // Convert UUID to a string representation, e.g. a121e9e1-feae-4136-9e0e-6fac343d56c9.
  static const char* const hex = "0123456789abcdef";
  char uuid[UUID_LENGTH];

  for (uint8_t i = 0; i < 4; i++) {
    const uint8_t d = rand[i];
    uuid[2 * i] = hex[d >> 4];
    uuid[2 * i + 1] = hex[d & 0x0f];
  }

  uuid[8] = '-';

  for (uint8_t i = 4; i < 6; i++) {
    const uint8_t d = rand[i];
    uuid[2 * i + 1] = hex[d >> 4];
    uuid[2 * i + 2] = hex[d & 0x0f];
  }

  uuid[13] = '-';

  for (uint8_t i = 6; i < 8; i++) {
    const uint8_t d = rand[i];
    uuid[2 * i + 2] = hex[d >> 4];
    uuid[2 * i + 3] = hex[d & 0x0f];
  }

  uuid[18] = '-';

  for (uint8_t i = 8; i < 10; i++) {
    const uint8_t d = rand[i];
    uuid[2 * i + 3] = hex[d >> 4];
    uuid[2 * i + 4] = hex[d & 0x0f];
  }

  uuid[23] = '-';

  for (uint8_t i = 10; i < 16; i++) {
    const uint8_t d = rand[i];
    uuid[2 * i + 4] = hex[d >> 4];
    uuid[2 * i + 5] = hex[d & 0x0f];
  }

  return std::string(uuid, UUID_LENGTH);
}

SnapshotImpl::SnapshotImpl(const std::string& root_path, const std::string& override_path,
                           RuntimeStats& stats, RandomGenerator& generator)
    : generator_(generator) {
  try {
    walkDirectory(root_path, "");
    if (Filesystem::directoryExists(override_path)) {
      walkDirectory(override_path, "");
      stats.override_dir_exists_.inc();
    } else {
      stats.override_dir_not_exists_.inc();
    }

    stats.load_success_.inc();
  } catch (EnvoyException& e) {
    stats.load_error_.inc();
    ENVOY_LOG(debug, "error creating runtime snapshot: {}", e.what());
  }

  stats.num_keys_.set(values_.size());
}

const std::string& SnapshotImpl::get(const std::string& key) const {
  auto entry = values_.find(key);
  if (entry == values_.end()) {
    return EMPTY_STRING;
  } else {
    return entry->second.string_value_;
  }
}

uint64_t SnapshotImpl::getInteger(const std::string& key, uint64_t default_value) const {
  auto entry = values_.find(key);
  if (entry == values_.end() || !entry->second.uint_value_.valid()) {
    return default_value;
  } else {
    return entry->second.uint_value_.value();
  }
}

void SnapshotImpl::walkDirectory(const std::string& path, const std::string& prefix) {
  ENVOY_LOG(debug, "walking directory: {}", path);
  Directory current_dir(path);
  while (true) {
    errno = 0;
    dirent* entry = readdir(current_dir.dir_);
    if (entry == nullptr && errno != 0) {
      throw EnvoyException(fmt::format("unable to iterate directory: {}", path));
    }

    if (entry == nullptr) {
      break;
    }

    std::string full_path = path + "/" + entry->d_name;
    std::string full_prefix;
    if (prefix.empty()) {
      full_prefix = entry->d_name;
    } else {
      full_prefix = prefix + "." + entry->d_name;
    }

    if (entry->d_type == DT_DIR && std::string(entry->d_name) != "." &&
        std::string(entry->d_name) != "..") {
      walkDirectory(full_path, full_prefix);
    } else if (entry->d_type == DT_REG) {
      // Suck the file into a string. This is not very efficient but it should be good enough
      // for small files. Also, as noted elsewhere, none of this is non-blocking which could
      // theoretically lead to issues.
      ENVOY_LOG(debug, "reading file: {}", full_path);
      Entry entry;
      entry.string_value_ = Filesystem::fileReadToEnd(full_path);
      StringUtil::rtrim(entry.string_value_);

      // As a perf optimization, attempt to convert the string into an integer. If we don't
      // succeed that's fine.
      uint64_t converted;
      if (StringUtil::atoul(entry.string_value_.c_str(), converted)) {
        entry.uint_value_.value(converted);
      }

      values_[full_prefix] = entry;
    }
  }
}

LoaderImpl::LoaderImpl(Event::Dispatcher& dispatcher, ThreadLocal::SlotAllocator& tls,
                       const std::string& root_symlink_path, const std::string& subdir,
                       const std::string& override_dir, Stats::Store& store,
                       RandomGenerator& generator)
    : watcher_(dispatcher.createFilesystemWatcher()), tls_(tls.allocateSlot()),
      generator_(generator), root_path_(root_symlink_path + "/" + subdir),
      override_path_(root_symlink_path + "/" + override_dir), stats_(generateStats(store)) {
  watcher_->addWatch(root_symlink_path, Filesystem::Watcher::Events::MovedTo,
                     [this](uint32_t) -> void { onSymlinkSwap(); });

  onSymlinkSwap();
}

RuntimeStats LoaderImpl::generateStats(Stats::Store& store) {
  std::string prefix = "runtime.";
  RuntimeStats stats{
      ALL_RUNTIME_STATS(POOL_COUNTER_PREFIX(store, prefix), POOL_GAUGE_PREFIX(store, prefix))};
  return stats;
}

void LoaderImpl::onSymlinkSwap() {
  current_snapshot_.reset(new SnapshotImpl(root_path_, override_path_, stats_, generator_));
  ThreadLocal::ThreadLocalObjectSharedPtr ptr_copy = current_snapshot_;
  tls_->set([ptr_copy](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return ptr_copy;
  });
}

Snapshot& LoaderImpl::snapshot() { return tls_->getTyped<Snapshot>(); }

} // namespace Runtime
} // namespace Envoy
