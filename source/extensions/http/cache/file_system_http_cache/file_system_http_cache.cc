#include "source/extensions/http/cache/file_system_http_cache/file_system_http_cache.h"

#include <chrono>

#include "source/common/filesystem/directory.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_eviction_thread.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_fixed_block.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_header_proto_util.h"
#include "source/extensions/http/cache/file_system_http_cache/insert_context.h"
#include "source/extensions/http/cache/file_system_http_cache/lookup_context.h"
#include "source/extensions/http/cache/file_system_http_cache/stats.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

// Copying in 128K chunks is an arbitrary choice for a reasonable balance of performance and
// memory usage. Since UpdateHeaders is unlikely to be a common operation it is most likely
// not worthwhile to carefully tune this.
const size_t FileSystemHttpCache::max_update_headers_copy_chunk_size_ = 128 * 1024;

const CacheStats& FileSystemHttpCache::stats() const { return shared_->stats_; }
const ConfigProto& FileSystemHttpCache::config() const { return shared_->config_; }

void FileSystemHttpCache::writeVaryNodeToDisk(const Key& key,
                                              const Http::ResponseHeaderMap& response_headers,
                                              std::shared_ptr<Cleanup> cleanup) {
  auto vary_values = VaryHeaderUtils::getVaryValues(response_headers);
  auto headers = std::make_shared<CacheFileHeader>();
  auto h = headers->add_headers();
  h->set_key("vary");
  h->set_value(absl::StrJoin(vary_values, ","));
  std::string filename = absl::StrCat(cachePath(), generateFilename(key));
  async_file_manager_->createAnonymousFile(
      cachePath(), [headers, filename = std::move(filename),
                    cleanup](absl::StatusOr<AsyncFileHandle> open_result) {
        if (!open_result.ok()) {
          ENVOY_LOG(warn, "writing vary node, failed to createAnonymousFile: {}",
                    open_result.status());
          return;
        }
        auto file_handle = std::move(open_result.value());
        CacheFileFixedBlock block;
        auto buf = bufferFromProto(*headers);
        block.setHeadersSize(buf.length());
        Buffer::OwnedImpl buf2;
        block.serializeToBuffer(buf2);
        buf2.add(buf);
        size_t sz = buf2.length();
        auto queued = file_handle->write(
            buf2, 0,
            [file_handle, cleanup, sz,
             filename = std::move(filename)](absl::StatusOr<size_t> write_result) {
              if (!write_result.ok() || write_result.value() != sz) {
                ENVOY_LOG(warn, "writing vary node, failed to write: {}", write_result.status());
                file_handle->close([](absl::Status) {}).IgnoreError();
                return;
              }
              auto queued = file_handle->createHardLink(
                  filename, [cleanup, file_handle](absl::Status link_result) {
                    if (!link_result.ok()) {
                      ENVOY_LOG(warn, "writing vary node, failed to link: {}", link_result);
                    }
                    file_handle->close([](absl::Status) {}).IgnoreError();
                  });
              ASSERT(queued.ok());
            });
        ASSERT(queued.ok());
      });
}

absl::string_view FileSystemHttpCache::name() {
  return "envoy.extensions.http.cache.file_system_http_cache";
}

FileSystemHttpCache::FileSystemHttpCache(
    Singleton::InstanceSharedPtr owner, CacheEvictionThread& cache_eviction_thread,
    ConfigProto config, std::shared_ptr<Common::AsyncFiles::AsyncFileManager>&& async_file_manager,
    Stats::Scope& stats_scope)
    : owner_(owner), async_file_manager_(async_file_manager),
      shared_(std::make_shared<CacheShared>(config, stats_scope)),
      cache_eviction_thread_(cache_eviction_thread) {
  cache_eviction_thread_.addCache(shared_);
}

CacheShared::CacheShared(ConfigProto config, Stats::Scope& stats_scope)
    : config_(config), stat_names_(stats_scope.symbolTable()),
      stats_(generateStats(stat_names_, stats_scope, cachePath())) {}

FileSystemHttpCache::~FileSystemHttpCache() { cache_eviction_thread_.removeCache(shared_); }

CacheInfo FileSystemHttpCache::cacheInfo() const {
  CacheInfo info;
  info.name_ = name();
  info.supports_range_requests_ = true;
  return info;
}

absl::optional<Key>
FileSystemHttpCache::makeVaryKey(const Key& base, const VaryAllowList& vary_allow_list,
                                 const absl::btree_set<absl::string_view>& vary_header_values,
                                 const Http::RequestHeaderMap& request_headers) {
  const absl::optional<std::string> vary_identifier =
      VaryHeaderUtils::createVaryIdentifier(vary_allow_list, vary_header_values, request_headers);
  if (!vary_identifier.has_value()) {
    // Skip the insert if we are unable to create a vary key.
    return absl::nullopt;
  }
  Key vary_key = base;
  vary_key.add_custom_fields(vary_identifier.value());
  return vary_key;
}

LookupContextPtr FileSystemHttpCache::makeLookupContext(LookupRequest&& lookup,
                                                        Http::StreamDecoderFilterCallbacks&) {
  return std::make_unique<FileLookupContext>(*this, std::move(lookup));
}

// Helper class to reduce the lambda depth of updateHeaders.
class HeaderUpdateContext : public Logger::Loggable<Logger::Id::cache_filter> {
public:
  HeaderUpdateContext(const FileSystemHttpCache& cache, const Key& key,
                      std::shared_ptr<Cleanup> cleanup,
                      const Http::ResponseHeaderMap& response_headers,
                      const ResponseMetadata& metadata, std::function<void(bool)> on_complete)
      : filepath_(absl::StrCat(cache.cachePath(), cache.generateFilename(key))),
        cache_path_(cache.cachePath()), cleanup_(cleanup),
        async_file_manager_(cache.asyncFileManager()),
        response_headers_(Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers)),
        response_metadata_(metadata), on_complete_(on_complete) {}

  void begin(std::shared_ptr<HeaderUpdateContext> ctx) {
    async_file_manager_->openExistingFile(filepath_,
                                          Common::AsyncFiles::AsyncFileManager::Mode::ReadOnly,
                                          [ctx, this](absl::StatusOr<AsyncFileHandle> open_result) {
                                            if (!open_result.ok()) {
                                              fail("failed to open", open_result.status());
                                              return;
                                            }
                                            read_handle_ = std::move(open_result.value());
                                            unlinkOriginal(ctx);
                                          });
  }

  ~HeaderUpdateContext() {
    // For chaining the close actions in a file thread, the closes must be chained sequentially.
    // write_handle_ can only be set if read_handle_ is set, so this ordering is safe.
    if (read_handle_) {
      read_handle_
          ->close([write_handle = write_handle_](absl::Status) {
            if (write_handle) {
              write_handle->close([](absl::Status) {}).IgnoreError();
            }
          })
          .IgnoreError();
    }
  }

private:
  void unlinkOriginal(std::shared_ptr<HeaderUpdateContext> ctx) {
    async_file_manager_->unlink(filepath_, [ctx, this](absl::Status unlink_result) {
      if (!unlink_result.ok()) {
        ENVOY_LOG(warn, "file_system_http_cache: {} for update cache file {}: {}", "unlink failed",
                  filepath_, unlink_result);
        // But keep going, because unlink might have failed because the file was already
        // deleted after we opened it. Worth a try to replace it!
      }
      readHeaderBlock(ctx);
    });
  }
  void readHeaderBlock(std::shared_ptr<HeaderUpdateContext> ctx) {
    auto queued = read_handle_->read(
        0, CacheFileFixedBlock::size(),
        [ctx, this](absl::StatusOr<Buffer::InstancePtr> read_result) {
          if (!read_result.ok() || read_result.value()->length() != CacheFileFixedBlock::size()) {
            fail("failed to read header block", read_result.status());
            return;
          }
          header_block_.populateFromStringView(read_result.value()->toString());
          readHeaders(ctx);
        });
    ASSERT(queued.ok());
  }
  void readHeaders(std::shared_ptr<HeaderUpdateContext> ctx) {
    auto queued = read_handle_->read(
        header_block_.offsetToHeaders(), header_block_.headerSize(),
        [ctx, this](absl::StatusOr<Buffer::InstancePtr> read_result) {
          if (!read_result.ok() || read_result.value()->length() != header_block_.headerSize()) {
            fail("failed to read headers", read_result.status());
            return;
          }
          header_proto_ = makeCacheFileHeaderProto(*read_result.value());
          if (header_proto_.headers_size() == 1 && header_proto_.headers(0).key() == "vary") {
            // TODO(ravenblack): do we need to handle vary entries here? How
            // did we get to updateHeaders on a vary entry rather than the
            // variant? Just abort for now.
            // (The entry was deleted at this point, so we should eventually get
            // back into a usable state even if this is a valid event.)
            fail("not implemented updating vary header", absl::OkStatus());
            return;
          }
          header_proto_ = mergeProtoWithHeadersAndMetadata(header_proto_, *response_headers_,
                                                           response_metadata_);
          size_t new_header_size = headerProtoSize(header_proto_);
          header_size_difference_ = header_block_.headerSize() - new_header_size;
          header_block_.setHeadersSize(new_header_size);
          startWriting(ctx);
        });
    ASSERT(queued.ok());
  }
  void startWriting(std::shared_ptr<HeaderUpdateContext> ctx) {
    async_file_manager_->createAnonymousFile(
        cache_path_, [ctx, this](absl::StatusOr<AsyncFileHandle> create_result) {
          if (!create_result.ok()) {
            fail("failed to open new cache file", create_result.status());
            return;
          }
          write_handle_ = std::move(create_result.value());
          writeHeaderBlockAndHeaders(ctx);
        });
  }
  void writeHeaderBlockAndHeaders(std::shared_ptr<HeaderUpdateContext> ctx) {
    Buffer::OwnedImpl buf;
    header_block_.serializeToBuffer(buf);
    buf.add(bufferFromProto(header_proto_));
    auto sz = buf.length();
    auto queued =
        write_handle_->write(buf, 0, [ctx, sz, this](absl::StatusOr<size_t> write_result) {
          if (!write_result.ok() || write_result.value() != sz) {
            fail("failed to write header block and headers", write_result.status());
            return;
          }
          copyBodyAndTrailers(ctx, header_block_.offsetToBody());
        });
    ASSERT(queued.ok());
  }
  void copyBodyAndTrailers(std::shared_ptr<HeaderUpdateContext> ctx, off_t offset) {
    size_t sz = header_block_.offsetToEnd() - offset;
    if (sz == 0) {
      linkNewFile(ctx);
      return;
    }
    sz = std::min(sz, FileSystemHttpCache::max_update_headers_copy_chunk_size_);
    auto queued = read_handle_->read(
        offset + header_size_difference_, sz,
        [ctx, offset, sz, this](absl::StatusOr<Buffer::InstancePtr> read_result) {
          if (!read_result.ok() || read_result.value()->length() != sz) {
            fail("failed to read body chunk", read_result.status());
            return;
          }
          auto queued =
              write_handle_->write(*read_result.value(), offset,
                                   [ctx, offset, sz, this](absl::StatusOr<size_t> write_result) {
                                     if (!write_result.ok() || write_result.value() != sz) {
                                       fail("failed to write body chunk", write_result.status());
                                       return;
                                     }
                                     copyBodyAndTrailers(ctx, offset + sz);
                                   });
          ASSERT(queued.ok());
        });
    ASSERT(queued.ok());
  }
  void linkNewFile(std::shared_ptr<HeaderUpdateContext> ctx) {
    auto queued = write_handle_->createHardLink(filepath_, [ctx, this](absl::Status link_result) {
      if (!link_result.ok()) {
        fail("failed to link new cache file", link_result);
        return;
      }
      on_complete_(true);
    });
    ASSERT(queued.ok());
  }
  void fail(absl::string_view msg, absl::Status status) {
    ENVOY_LOG(warn, "file_system_http_cache: {} for update cache file {}: {}", msg, filepath_,
              status);
    on_complete_(false);
  }
  std::string filepath_;
  std::string cache_path_;
  std::shared_ptr<Cleanup> cleanup_;
  std::shared_ptr<Common::AsyncFiles::AsyncFileManager> async_file_manager_;
  Http::ResponseHeaderMapPtr response_headers_;
  ResponseMetadata response_metadata_;
  CacheFileFixedBlock header_block_;
  off_t header_size_difference_;
  CacheFileHeader header_proto_;
  AsyncFileHandle read_handle_;
  AsyncFileHandle write_handle_;
  std::function<void(bool)> on_complete_;
};

void FileSystemHttpCache::updateHeaders(const LookupContext& lookup_context,
                                        const Http::ResponseHeaderMap& response_headers,
                                        const ResponseMetadata& metadata,
                                        std::function<void(bool)> on_complete) {
  const Key& key = dynamic_cast<const FileLookupContext&>(lookup_context).key();
  auto cleanup = maybeStartWritingEntry(key);
  if (!cleanup) {
    return;
  }
  auto ctx = std::make_shared<HeaderUpdateContext>(*this, key, cleanup, response_headers, metadata,
                                                   on_complete);
  ctx->begin(ctx);
}

absl::string_view FileSystemHttpCache::cachePath() const { return shared_->cachePath(); }

bool FileSystemHttpCache::workInProgress(const Key& key) {
  absl::MutexLock lock(&cache_mu_);
  return entries_being_written_.contains(key);
}

std::shared_ptr<Cleanup> FileSystemHttpCache::maybeStartWritingEntry(const Key& key) {
  absl::MutexLock lock(&cache_mu_);
  if (!entries_being_written_.emplace(key).second) {
    return nullptr;
  }
  return std::make_shared<Cleanup>([this, key]() {
    absl::MutexLock lock(&cache_mu_);
    entries_being_written_.erase(key);
  });
}

std::shared_ptr<Cleanup>
FileSystemHttpCache::setCacheEntryToVary(const Key& key,
                                         const Http::ResponseHeaderMap& response_headers,
                                         const Key& varied_key, std::shared_ptr<Cleanup> cleanup) {
  writeVaryNodeToDisk(key, response_headers, cleanup);
  return maybeStartWritingEntry(varied_key);
}

std::string FileSystemHttpCache::generateFilename(const Key& key) const {
  // TODO(ravenblack): Add support for directory tree structure.
  return absl::StrCat("cache-", stableHashKey(key));
}

InsertContextPtr FileSystemHttpCache::makeInsertContext(LookupContextPtr&& lookup_context,
                                                        Http::StreamEncoderFilterCallbacks&) {
  auto file_lookup_context = std::unique_ptr<FileLookupContext>(
      dynamic_cast<FileLookupContext*>(lookup_context.release()));
  ASSERT(file_lookup_context);
  if (file_lookup_context->workInProgress()) {
    return std::make_unique<DontInsertContext>();
  }
  return std::make_unique<FileInsertContext>(shared_from_this(), std::move(file_lookup_context));
}

void FileSystemHttpCache::trackFileAdded(uint64_t file_size) {
  shared_->trackFileAdded(file_size);
  if (shared_->needsEviction()) {
    cache_eviction_thread_.signal();
  }
}
void CacheShared::trackFileAdded(uint64_t file_size) {
  size_count_++;
  size_bytes_ += file_size;
  stats_.size_count_.inc();
  stats_.size_bytes_.add(file_size);
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
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
