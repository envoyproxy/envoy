#include "source/extensions/http/cache/file_system_http_cache/cache_eviction_thread.h"

#include <limits>

#include "envoy/thread/thread.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/filesystem/directory.h"
#include "source/extensions/http/cache/file_system_http_cache/file_system_http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

namespace {
bool isCacheFile(const Filesystem::DirectoryEntry& entry) {
  return absl::StartsWith(entry.name_, "cache-") && entry.type_ == Filesystem::FileType::Regular;
}
} // namespace

CacheEvictionThread::CacheEvictionThread(Thread::ThreadFactory& thread_factory)
    : thread_(thread_factory.createThread([this]() { work(); })) {}

CacheEvictionThread::~CacheEvictionThread() {
  terminate();
  thread_->join();
}

void CacheEvictionThread::addCache(std::shared_ptr<CacheShared> cache) {
  {
    absl::MutexLock lock(&cache_mu_);
    bool inserted = caches_.emplace(std::move(cache)).second;
    ASSERT(inserted);
  }
  signal();
}

void CacheEvictionThread::removeCache(std::shared_ptr<CacheShared>& cache) {
  absl::MutexLock lock(&cache_mu_);
  bool removed = caches_.erase(cache);
  ASSERT(removed);
}

void CacheEvictionThread::signal() {
  absl::MutexLock lock(&mu_);
  signalled_ = true;
}

void CacheEvictionThread::terminate() {
  absl::MutexLock lock(&mu_);
  terminating_ = true;
  signalled_ = true;
}

bool CacheEvictionThread::waitForSignal() {
  absl::MutexLock lock(&mu_);
  // Worth noting here that if `signalled_` is already true, the lock is not released
  // until idle_ is false again, so waitForIdle will not return until `signalled_`
  // stays false for the duration of an eviction cycle.
  idle_ = true;
  mu_.Await(absl::Condition(&signalled_));
  signalled_ = false;
  idle_ = false;
  return !terminating_;
}

void CacheEvictionThread::init(CacheShared& cache) {
  if (cache.config_.has_max_cache_size_bytes()) {
    cache.stats_.size_limit_bytes_.set(cache.config_.max_cache_size_bytes().value());
  }
  if (cache.config_.has_max_cache_entry_count()) {
    cache.stats_.size_limit_count_.set(cache.config_.max_cache_entry_count().value());
  }
  // TODO(ravenblack): Add support for directory tree structure.
  for (const Filesystem::DirectoryEntry& entry :
       Filesystem::Directory(std::string{cache.cachePath()})) {
    if (!isCacheFile(entry)) {
      continue;
    }
    cache.size_count_++;
    cache.size_bytes_ += entry.size_bytes_.value_or(0);
  }
  cache.stats_.size_count_.set(cache.size_count_);
  cache.stats_.size_bytes_.set(cache.size_bytes_);
  cache.needs_init_ = false;
}

void CacheEvictionThread::evict(CacheShared& cache) {
  auto os_sys_calls = Api::OsSysCallsSingleton::get();
  uint64_t size = 0;
  uint64_t count = 0;
  struct CacheFile {
    std::string name_;
    uint64_t size_;
    Envoy::SystemTime last_touch_;
    // Reversed < operator because we're comparing timestamps but considering age, i.e.
    // a greater timestamp is a lesser age.
    bool operator<(const CacheFile& other) const { return other.last_touch_ < last_touch_; }
  };
  // A set of files sorted by last-touch timestamp, highest (i.e. youngest) first.
  std::multiset<CacheFile> cache_files;

  // TODO(ravenblack): Add support for directory tree structure.
  for (const Filesystem::DirectoryEntry& entry :
       Filesystem::Directory(std::string{cache.cachePath()})) {
    if (!isCacheFile(entry)) {
      continue;
    }
    count++;
    size += entry.size_bytes_.value_or(0);
    struct stat s;
    if (os_sys_calls.stat(absl::StrCat(cache.cachePath(), entry.name_).c_str(), &s).return_value_ !=
        -1) {
      Envoy::SystemTime last_touch =
          std::max(timespecToChrono(s.st_atim), timespecToChrono(s.st_ctim));

      cache_files.insert(CacheFile{entry.name_, entry.size_bytes_.value_or(0), last_touch});
    }
  }
  cache.size_bytes_ = size;
  cache.size_count_ = count;
  uint64_t size_kept = 0;
  uint64_t count_kept = 0;
  uint64_t max_size = cache.config_.has_max_cache_size_bytes()
                          ? cache.config_.max_cache_size_bytes().value()
                          : std::numeric_limits<uint64_t>::max();
  uint64_t max_count = cache.config_.has_max_cache_entry_count()
                           ? cache.config_.max_cache_entry_count().value()
                           : std::numeric_limits<uint64_t>::max();
  auto it = cache_files.begin();
  // Keep the youngest files that won't exceed the limit.
  while (it != cache_files.end() && size_kept + it->size_ <= max_size &&
         count_kept + 1 <= max_count) {
    size_kept += it->size_;
    count_kept++;
    ++it;
  }
  // Evict the rest.
  while (it != cache_files.end()) {
    if (os_sys_calls.unlink(absl::StrCat(cache.cachePath(), it->name_).c_str()).return_value_ !=
        -1) {
      // May want to add logging here for cache eviction failure, but it's expected sometimes,
      // e.g. if another instance of Envoy is performing cleanup at the same time, or some external
      // operator deleted the file. If it fails we don't reduce the estimated cache size, so another
      // eviction run will happen sooner.
      // TODO(ravenblack): might be worth checking the type of the error, or whether the file is
      // gone - if there's a permissions issue, for example, then the cache might remain oversized
      // and the eviction thread will be churning, trying and failing to remove a file, which would
      // be worth logging a warning, versus if the file is already gone then there's no problem.
      cache.trackFileRemoved(it->size_);
    }
    ++it;
  }
}

void CacheEvictionThread::work() {
  while (waitForSignal()) {
    absl::flat_hash_set<std::shared_ptr<CacheShared>> caches;
    {
      // Take a local copy of the set of caches, so we don't hold the lock while
      // work is being performed.
      absl::MutexLock lock(&cache_mu_);
      caches = caches_;
    }

    for (const std::shared_ptr<CacheShared>& cache : caches) {
      if (cache->needs_init_) {
        init(*cache);
      }
      if (cache->needsEviction()) {
        evict(*cache);
      }
    }
  }
}

void CacheEvictionThread::waitForIdle() {
  absl::MutexLock lock(&mu_);
  auto cond = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) { return idle_ && !signalled_; };
  mu_.Await(absl::Condition(&cond));
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
