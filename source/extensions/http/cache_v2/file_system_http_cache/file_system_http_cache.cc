#include "source/extensions/http/cache_v2/file_system_http_cache/file_system_http_cache.h"

#include <chrono>

#include "source/common/filesystem/directory.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/cache_eviction_thread.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/cache_file_fixed_block.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/cache_file_header_proto_util.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/insert_context.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/lookup_context.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/stats.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace FileSystemHttpCache {

const CacheStats& FileSystemHttpCache::stats() const { return shared_->stats_; }
const ConfigProto& FileSystemHttpCache::config() const { return shared_->config_; }

absl::string_view FileSystemHttpCache::name() {
  return "envoy.extensions.http.cache_v2.file_system_http_cache";
}

FileSystemHttpCache::FileSystemHttpCache(
    Singleton::InstanceSharedPtr owner, CacheEvictionThread& cache_eviction_thread,
    ConfigProto config, std::shared_ptr<Common::AsyncFiles::AsyncFileManager>&& async_file_manager,
    Stats::Scope& stats_scope)
    : owner_(owner), async_file_manager_(async_file_manager),
      shared_(std::make_shared<CacheShared>(config, stats_scope, cache_eviction_thread)),
      cache_eviction_thread_(cache_eviction_thread), cache_info_(CacheInfo{name()}) {
  cache_eviction_thread_.addCache(shared_);
}

CacheShared::CacheShared(ConfigProto config, Stats::Scope& stats_scope,
                         CacheEvictionThread& eviction_thread)
    : signal_eviction_([&eviction_thread]() { eviction_thread.signal(); }), config_(config),
      stat_names_(stats_scope.symbolTable()),
      stats_(generateStats(stat_names_, stats_scope, cachePath())) {}

void CacheShared::disconnectEviction() {
  absl::MutexLock lock(signal_mu_);
  signal_eviction_ = []() {};
}

FileSystemHttpCache::~FileSystemHttpCache() {
  shared_->disconnectEviction();
  cache_eviction_thread_.removeCache(shared_);
}

CacheInfo FileSystemHttpCache::cacheInfo() const {
  CacheInfo info;
  info.name_ = name();
  return info;
}

void FileSystemHttpCache::lookup(LookupRequest&& lookup, LookupCallback&& callback) {
  std::string filepath = absl::StrCat(cachePath(), generateFilename(lookup.key()));
  async_file_manager_->openExistingFile(
      &lookup.dispatcher(), filepath, Common::AsyncFiles::AsyncFileManager::Mode::ReadOnly,
      [&dispatcher = lookup.dispatcher(),
       callback = std::move(callback)](absl::StatusOr<AsyncFileHandle> open_result) mutable {
        if (!open_result.ok()) {
          if (open_result.status().code() == absl::StatusCode::kNotFound) {
            return callback(LookupResult{});
          }
          ENVOY_LOG(error, "open file failed: {}", open_result.status());
          return callback(open_result.status());
        }
        FileLookupContext::begin(dispatcher, std::move(open_result.value()), std::move(callback));
      });
}

void FileSystemHttpCache::insert(Event::Dispatcher& dispatcher, Key key,
                                 Http::ResponseHeaderMapPtr headers, ResponseMetadata metadata,
                                 HttpSourcePtr source,
                                 std::shared_ptr<CacheProgressReceiver> progress) {
  std::string filepath = absl::StrCat(cachePath(), generateFilename(key));
  FileInsertContext::begin(dispatcher, std::move(key), std::move(filepath), std::move(headers),
                           std::move(metadata), std::move(source), std::move(progress), shared_,
                           *async_file_manager_);
}

// Helper class to reduce the lambda depth of updateHeaders.
class HeaderUpdateContext : public Logger::Loggable<Logger::Id::cache_filter> {
public:
  static void begin(Event::Dispatcher& dispatcher, AsyncFileHandle handle,
                    Buffer::InstancePtr new_headers) {
    auto p = new HeaderUpdateContext(dispatcher, std::move(handle), std::move(new_headers));
    p->readHeaderBlock();
  }

private:
  HeaderUpdateContext(Event::Dispatcher& dispatcher, AsyncFileHandle handle,
                      Buffer::InstancePtr new_headers)
      : dispatcher_(dispatcher), handle_(std::move(handle)), new_headers_(std::move(new_headers)) {}

  void readHeaderBlock() {
    auto queued = handle_->read(
        &dispatcher_, 0, CacheFileFixedBlock::size(),
        [this](absl::StatusOr<Buffer::InstancePtr> read_result) {
          if (!read_result.ok()) {
            return fail("failed to read header block", read_result.status());
          } else if (read_result.value()->length() != CacheFileFixedBlock::size()) {
            return fail(
                "incomplete read of header block",
                absl::AbortedError(absl::StrCat("read ", read_result.value()->length(),
                                                ", expected ", CacheFileFixedBlock::size())));
          }
          header_block_.populateFromStringView(read_result.value()->toString());
          truncateOldHeaders();
        });
    ASSERT(queued.ok());
  }

  void truncateOldHeaders() {
    auto queued = handle_->truncate(&dispatcher_, header_block_.offsetToHeaders(),
                                    [this](absl::Status truncate_result) {
                                      if (!truncate_result.ok()) {
                                        return fail("failed to truncate headers", truncate_result);
                                      }
                                      overwriteHeaderBlock();
                                    });
    ASSERT(queued.ok());
  }

  void overwriteHeaderBlock() {
    size_t len = new_headers_->length();
    header_block_.setHeadersSize(len);
    Buffer::OwnedImpl write_buf;
    header_block_.serializeToBuffer(write_buf);
    auto queued =
        handle_->write(&dispatcher_, write_buf, 0, [this](absl::StatusOr<size_t> write_result) {
          if (!write_result.ok()) {
            return fail("overwriting headers failed", write_result.status());
          } else if (write_result.value() != CacheFileFixedBlock::size()) {
            return fail(
                "overwriting headers failed",
                absl::AbortedError(absl::StrCat("wrote ", write_result.value(), ", expected ",
                                                CacheFileFixedBlock::size())));
          }
          writeNewHeaders();
        });
    ASSERT(queued.ok());
  }

  void writeNewHeaders() {
    size_t len = new_headers_->length();
    auto queued =
        handle_->write(&dispatcher_, *new_headers_, header_block_.offsetToHeaders(),
                       [this, len](absl::StatusOr<size_t> write_result) {
                         if (!write_result.ok()) {
                           return fail("failed to write new headers", write_result.status());
                         } else if (write_result.value() != len) {
                           return fail("incomplete write of new headers",
                                       absl::AbortedError(absl::StrCat(
                                           "wrote ", write_result.value(), ", expected ", len)));
                         }
                         finish();
                       });
    ASSERT(queued.ok());
  }

  void fail(absl::string_view msg, absl::Status status) {
    ENVOY_LOG(error, "{}: {}", msg, status);
    finish();
  }

  void finish() {
    auto close_status = handle_->close(nullptr, [](absl::Status) {});
    ASSERT(close_status.ok());
    delete this;
  }

  Event::Dispatcher& dispatcher_;
  AsyncFileHandle handle_;
  Buffer::InstancePtr new_headers_;
  CacheFileFixedBlock header_block_;
};

/**
 * Replaces the headers of a cache entry.
 *
 * In order to avoid a race in which the wrong size of headers is read by
 * one instance while headers are being updated by another instance, the
 * update is performed by:
 * 1. truncate the file so there are no headers.
 * 2. update the size of the headers in the header block.
 * 3. write the new headers.
 *
 * This way, if another instance tries to read headers when they are briefly
 * not present, that read will fail to get the expected size, and it will be
 * treated as a cache miss rather than providing a "mixed" (corrupted) read.
 *
 * Most of the time the cache is not reading headers from the file as they
 * are cached in memory, so even this race should be extremely rare.
 */
void FileSystemHttpCache::updateHeaders(Event::Dispatcher& dispatcher, const Key& key,
                                        const Http::ResponseHeaderMap& updated_headers,
                                        const ResponseMetadata& updated_metadata) {
  std::string filepath = absl::StrCat(cachePath(), generateFilename(key));
  CacheFileHeader header_proto = makeCacheFileHeaderProto(key, updated_headers, updated_metadata);
  Buffer::InstancePtr header_buffer = std::make_unique<Buffer::OwnedImpl>();
  Buffer::OwnedImpl tmp = bufferFromProto(header_proto);
  header_buffer->move(tmp);
  async_file_manager_->openExistingFile(
      &dispatcher, filepath, Common::AsyncFiles::AsyncFileManager::Mode::ReadWrite,
      [&dispatcher = dispatcher, header_buffer = std::move(header_buffer)](
          absl::StatusOr<AsyncFileHandle> open_result) mutable {
        if (!open_result.ok()) {
          ENVOY_LOG(error, "open file for updateHeaders failed: {}", open_result.status());
          return;
        }
        HeaderUpdateContext::begin(dispatcher, open_result.value(), std::move(header_buffer));
      });
}

void FileSystemHttpCache::evict(Event::Dispatcher& dispatcher, const Key& key) {
  std::string filepath = absl::StrCat(cachePath(), generateFilename(key));
  async_file_manager_->stat(&dispatcher, filepath,
                            [file_manager = async_file_manager_, &dispatcher, filepath,
                             stats = shared_](absl::StatusOr<struct stat> stat_result) {
                              if (!stat_result.ok()) {
                                return;
                              }
                              off_t sz = stat_result.value().st_size;
                              file_manager->unlink(&dispatcher, filepath,
                                                   [sz, stats](absl::Status unlink_result) {
                                                     if (!unlink_result.ok()) {
                                                       return;
                                                     }
                                                     stats->trackFileRemoved(sz);
                                                   });
                            });
}

void FileSystemHttpCache::touch(const Key&, SystemTime) {
  // Reading from a file counts as a touch for stat purposes, so no
  // need to update timestamps directly.
}

absl::string_view FileSystemHttpCache::cachePath() const { return shared_->cachePath(); }

std::string FileSystemHttpCache::generateFilename(const Key& key) const {
  // TODO(ravenblack): Add support for directory tree structure.
  return absl::StrCat("cache-", stableHashKey(key));
}

void FileSystemHttpCache::trackFileAdded(uint64_t file_size) { shared_->trackFileAdded(file_size); }
void CacheShared::trackFileAdded(uint64_t file_size) {
  size_count_++;
  size_bytes_ += file_size;
  stats_.size_count_.inc();
  stats_.size_bytes_.add(file_size);
  if (needsEviction()) {
    {
      absl::MutexLock lock(signal_mu_);
      signal_eviction_();
    }
  }
}

void FileSystemHttpCache::trackFileRemoved(uint64_t file_size) {
  shared_->trackFileRemoved(file_size);
}

void CacheShared::trackFileRemoved(uint64_t file_size) {
  // Atomically decrement-but-clamp-at-zero the count of files in the cache.
  //
  // It is an error to try to set a gauge to less than zero, so we must actively
  // prevent that underflow.
  //
  // See comment on size_bytes and size_count in stats.h for explanation of how stat
  // values can be out of sync with the actionable cache.
  uint64_t count, size;
  do {
    count = size_count_;
  } while (count > 0 && !size_count_.compare_exchange_weak(count, count - 1));

  stats_.size_count_.set(size_count_);
  // Atomically decrease-but-clamp-at-zero the size of files in the cache, by file_size.
  //
  // See comment above for why; the same rationale applies here.
  do {
    size = size_bytes_;
  } while (size >= file_size && !size_bytes_.compare_exchange_weak(size, size - file_size));

  if (size < file_size) {
    size_bytes_ = 0;
  }
  stats_.size_bytes_.set(size_bytes_);
}

bool CacheShared::needsEviction() const {
  if (config_.has_max_cache_size_bytes() && size_bytes_ > config_.max_cache_size_bytes().value()) {
    return true;
  }
  if (config_.has_max_cache_entry_count() &&
      size_count_ > config_.max_cache_entry_count().value()) {
    return true;
  }
  return false;
}

} // namespace FileSystemHttpCache
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
