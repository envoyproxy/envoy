#include "source/extensions/filters/http/cache/file_system_http_cache/file_system_http_cache.h"

#include "source/extensions/filters/http/cache/file_system_http_cache/cache_file_header_proto_util.h"
#include "source/extensions/filters/http/cache/file_system_http_cache/insert_context.h"
#include "source/extensions/filters/http/cache/file_system_http_cache/lookup_context.h"

#include "cache_entry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

// Not a big deal if this write fails since vary cache entries are fully in-memory anyway -
// the only use of the file is for repopulating the cache at startup.
void FileSystemHttpCache::writeVaryNodeToDisk(const Key& key,
                                              std::shared_ptr<CacheEntryVaryRedirect> vary_node) {
  std::string filename = absl::StrCat(cachePath(), generateFilename(key));
  async_file_manager_->createAnonymousFile(
      cachePath(),
      [vary_node, filename = std::move(filename)](absl::StatusOr<AsyncFileHandle> open_result) {
        if (open_result.ok()) {
          auto file_handle = std::move(open_result.value());
          auto vary_node_content = vary_node->asBuffer();
          size_t expected_write_size = vary_node_content.length();
          auto write_queued = file_handle->write(
              vary_node_content, 0,
              [file_handle, filename = std::move(filename),
               expected_write_size](absl::StatusOr<size_t> write_result) {
                if (write_result.ok() && write_result.value() == expected_write_size) {
                  auto link_queued =
                      file_handle->createHardLink(filename, [file_handle](absl::Status) {
                        auto close_queued = file_handle->close([](absl::Status) {});
                        ASSERT(close_queued.ok());
                      });
                  ASSERT(link_queued.ok());
                } else {
                  auto close_queued = file_handle->close([](absl::Status) {});
                  ASSERT(close_queued.ok());
                }
              });
          ASSERT(write_queued.ok());
        }
      });
}

absl::string_view FileSystemHttpCache::name() {
  return "envoy.extensions.filters.http.cache.file_system_http_cache";
}

FileSystemHttpCache::FileSystemHttpCache(
    Singleton::InstanceSharedPtr owner, ConfigProto config, TimeSource& time_source,
    std::shared_ptr<Common::AsyncFiles::AsyncFileManager>&& async_file_manager)
    : owner_(owner), config_(config), time_source_(time_source),
      async_file_manager_(async_file_manager) {}

CacheInfo FileSystemHttpCache::cacheInfo() const {
  CacheInfo info;
  info.name_ = name();
  info.supports_range_requests_ = true;
  return info;
}

LookupContextPtr FileSystemHttpCache::makeLookupContext(LookupRequest&& lookup,
                                                        Http::StreamDecoderFilterCallbacks&) {
  auto& key = lookup.key();
  absl::MutexLock lock(&cache_mu_);
  auto cache_entry = cache_.find(key);
  if (cache_entry == cache_.end()) {
    // Nothing in the cache right now.
    return std::make_unique<NoOpLookupContext>(std::move(lookup), false);
  }
  cache_entry->second->touch(time_source_);
  auto vary_entry = dynamic_cast<CacheEntryVaryRedirect*>(cache_entry->second.get());
  if (vary_entry != nullptr) {
    // A vary entry is what's in the cache. Get the key for this new entry.
    auto maybe_vary_key = vary_entry->varyKey(key, lookup.varyAllowList(), lookup.requestHeaders());
    if (!maybe_vary_key.has_value()) {
      // Skip the lookup or insert if we are unable to create a vary key.
      return std::make_unique<NoOpLookupContext>(std::move(lookup), true);
    }
    Key& vary_key = maybe_vary_key.value();
    cache_entry = cache_.find(vary_key);
    if (cache_entry == cache_.end()) {
      // Nothing in the cache at the vary key right now.
      return std::make_unique<NoOpLookupContext>(std::move(lookup), false);
    } else {
      cache_entry->second->touch(time_source_);
    }
  }
  auto file_entry = std::dynamic_pointer_cast<CacheEntryFile>(cache_entry->second);
  bool refuse_inserts = false;
  if (!file_entry) {
    auto wip_entry = std::dynamic_pointer_cast<CacheEntryWorkInProgress>(cache_entry->second);
    // There shouldn't be any other types so it must be WorkInProgress.
    ASSERT(wip_entry);
    file_entry = wip_entry->entryBeingReplaced();
    // We shouldn't allow insert operations when there's already a WorkInProgress.
    refuse_inserts = true;
  }
  if (!file_entry) {
    // If we got here the cache entry is probably a "work in progress" entry where there
    // was no previous entry - we treat that as "not found" for now, but also no insert.
    return std::make_unique<NoOpLookupContext>(std::move(lookup), refuse_inserts);
  }
  // The cache is already populated for this lookup, so we can just share that entry.
  // We still must capture the lookup in case we have to change key for a vary header.
  return std::make_unique<FileLookupContext>(
      *this, std::dynamic_pointer_cast<CacheEntryFile>(cache_entry->second), std::move(lookup),
      refuse_inserts);
}

void FileSystemHttpCache::updateHeaders(const LookupContext& lookup_context,
                                        const Http::ResponseHeaderMap& response_headers,
                                        const ResponseMetadata& metadata) {
  const auto* file_lookup_context = dynamic_cast<const FileLookupContext*>(&lookup_context);
  // Don't update headers for NoOpLookupContext!
  if (file_lookup_context == nullptr) {
    return;
  }
  const Key& key = file_lookup_context->lookup().key();
  absl::MutexLock lock(&cache_mu_);

  auto it = cache_.find(key);
  if (it == cache_.end()) {
    return;
  }
  auto entry = std::dynamic_pointer_cast<CacheEntryFile>(it->second);
  if (!entry) {
    auto vary = std::dynamic_pointer_cast<CacheEntryVaryRedirect>(it->second);
    if (vary) {
      // TODO(ravenblack): handle Vary entry updates properly
      return;
    }
    // Don't update headers for non-file non-vary cache entries.
    return;
  }

  auto header_data = entry->getHeaderData();
  updateProtoFromHeadersAndMetadata(header_data.proto, response_headers, metadata);
  header_data.block.setHeadersSize(headerProtoSize(header_data.proto));
  entry->setHeaderData(header_data.block, header_data.proto);
  updateHeadersInFile(absl::StrCat(cachePath(), entry->filename()), header_data);
}

absl::string_view FileSystemHttpCache::cachePath() const { return config_.cache_path(); }

void FileSystemHttpCache::updateHeadersInFile(std::string filepath,
                                              CacheEntryFile::HeaderData header_data) {
  async_file_manager_->openExistingFile(
      filepath, AsyncFileManager::Mode::ReadWrite,
      [header_data, filepath](absl::StatusOr<AsyncFileHandle> open_result) {
        if (!open_result.ok()) {
          ENVOY_LOG(warn, "file_system_http_cache: failed to open for update cache file {}",
                    filepath);
          // Don't have to clean it up though, we can still use it from memory.
          return;
        }
        AsyncFileHandle file_handle = std::move(open_result.value());
        auto buf = bufferFromProto(header_data.proto);
        size_t sz = buf.length();
        auto queued = file_handle->write(
            buf, header_data.block.offsetToHeaders(),
            [sz, header_data, filepath, file_handle](absl::StatusOr<size_t> write_result) {
              if (!write_result.ok() || write_result.value() != sz) {
                absl::Status close_queued = file_handle->close([](absl::Status) {});
                ASSERT(close_queued.ok());
                ENVOY_LOG(warn,
                          "file_system_http_cache: failed to replace headers for cache file {}",
                          filepath);
                // Don't have to clean it up though, we can still use it from memory.
                return;
              }
              Buffer::OwnedImpl buf{header_data.block.stringView()};
              auto queued = file_handle->write(
                  buf, 0, [filepath, file_handle](absl::StatusOr<size_t> write_result) {
                    absl::Status close_queued = file_handle->close([](absl::Status) {});
                    ASSERT(close_queued.ok());
                    if (!write_result.ok() || write_result.value() != CacheFileFixedBlock::size()) {
                      ENVOY_LOG(warn,
                                "file_system_http_cache: failed to replace header block for cache "
                                "file {}",
                                filepath);
                      // Don't have to clean it up though, we can still use it from memory.
                    }
                  });
              ASSERT(queued.ok());
            });
        ASSERT(queued.ok());
      });
}

void FileSystemHttpCache::removeLeastRecentlyUsed() {
  // TODO(ravenblack): not yet implemented.
  // Note - do not remove "work in progress" entries.
  // 1. Set FileCacheEntry::on_destroy_ to a callback that async deletes the file.
  // 2. Remove the entry from the cache.
}

void FileSystemHttpCache::populateFromDisk() {
  // TODO(ravenblack): not yet implemented.
  // Scan through all files matching path + "cache-*"
  // Note - distinguish 'vary' entries by the fact that they are header-only and the only header
  // is 'vary'.
  // Undecided: Is it okay to do this synchronously?
}

std::shared_ptr<CacheEntryFile> FileSystemHttpCache::setCacheEntryWorkInProgress(const Key& key) {
  absl::MutexLock lock(&cache_mu_);
  return setCacheEntryWorkInProgressLocked(key);
}

std::shared_ptr<CacheEntryFile>
FileSystemHttpCache::setCacheEntryWorkInProgressLocked(const Key& key) {
  cache_mu_.AssertHeld();
  auto it = cache_.find(key);
  if (it == cache_.end()) {
    cache_.emplace(key, CacheEntryWorkInProgress::create(nullptr));
  } else {
    auto file = std::dynamic_pointer_cast<CacheEntryFile>(it->second);
    if (!file) {
      // We shouldn't be trying this on a vary entry, so this must be a WorkInProgress.
      ASSERT(std::dynamic_pointer_cast<CacheEntryWorkInProgress>(it->second));
      return nullptr;
    }
    it->second = CacheEntryWorkInProgress::create(file);
  }
  return CacheEntryFile::create(*async_file_manager_, generateFilename(key));
}

std::shared_ptr<CacheEntryFile> FileSystemHttpCache::setCacheEntryToVary(
    const Key& key, const Http::ResponseHeaderMap& response_headers, const Key& varied_key) {
  absl::MutexLock lock(&cache_mu_);
  auto it = cache_.find(key);
  std::shared_ptr<CacheEntryVaryRedirect> vary_node;
  if (it == cache_.end()) {
    vary_node = CacheEntryVaryRedirect::create(VaryHeaderUtils::getVaryValues(response_headers));
    cache_.emplace(key, vary_node);
    writeVaryNodeToDisk(key, vary_node);
  } else {
    vary_node = std::dynamic_pointer_cast<CacheEntryVaryRedirect>(it->second);
  }
  vary_node->touch(time_source_);
  return setCacheEntryWorkInProgressLocked(varied_key);
}

void FileSystemHttpCache::updateEntryToFile(const Key& key,
                                            std::shared_ptr<CacheEntryFile>&& file_entry) {
  ENVOY_LOG(debug, "cache entry inserted for {}{}", key.host(), key.path());
  absl::MutexLock lock(&cache_mu_);
  auto it = cache_.find(key);
  ASSERT(it != cache_.end());
  auto old_entry = std::dynamic_pointer_cast<CacheEntryFile>(it->second);
  if (!old_entry) {
    auto old_wip = dynamic_cast<CacheEntryWorkInProgress*>(it->second.get());
    ASSERT(old_wip); // It must be one of CacheEntryFile or CacheEntryWorkInProgress.
    old_entry = old_wip->entryBeingReplaced();
  }
  it->second = std::move(file_entry);
  if (old_entry) {
    purgeCacheFile(std::move(old_entry));
  }
}

void FileSystemHttpCache::removeCacheEntryInProgress(const Key& key) {
  absl::MutexLock lock(&cache_mu_);
  auto it = cache_.find(key);
  ASSERT(it != cache_.end());
  ASSERT(dynamic_cast<CacheEntryWorkInProgress*>(it->second.get()));
  cache_.erase(it);
}

std::string FileSystemHttpCache::generateFilename(const Key& key) {
  // Chances of a 64-bit hash collision are small; we reduce the chances even further by
  // having an arbitrary 32-bit rotating suffix, since the filenames don't even actually
  // matter - they're indexed by key anyway.
  // It would be plausible for a client to deliberately provoke a hash collision, but
  // provoking a few million writes *and* a hash collision at the exact right moment
  // would be very hard.
  // TODO(ravenblack): use timestamp instead for the suffix?
  return absl::StrCat("cache-", stableHashKey(key), "-", filename_suffix_++);
}

InsertContextPtr FileSystemHttpCache::makeInsertContext(LookupContextPtr&& lookup_context,
                                                        Http::StreamEncoderFilterCallbacks&) {
  auto noop_lookup = dynamic_cast<NoOpLookupContext*>(lookup_context.get());
  ASSERT(noop_lookup);
  if (noop_lookup->refuseInserts()) {
    return std::make_unique<DontInsertContext>();
  }
  auto& lookup = noop_lookup->lookup();
  return std::make_unique<FileInsertContext>(shared_from_this(), lookup.key(),
                                             lookup.requestHeaders(), lookup.varyAllowList());
}

void FileSystemHttpCache::removeCacheEntry(const Key& key, std::shared_ptr<CacheEntryFile>&& value,
                                           PurgeOption purge_option) {
  absl::MutexLock lock(&cache_mu_);
  auto it = cache_.find(key);
  // Don't remove if the entry has already been removed by another worker.
  if (it == cache_.end() || it->second != value) {
    return;
  }
  cache_.erase(it);
  if (purge_option == PurgeOption::PurgeFile) {
    purgeCacheFile(std::move(value));
  }
}

void FileSystemHttpCache::purgeCacheFile(std::shared_ptr<CacheEntryFile>&& cache_entry) {
  std::string filename = absl::StrCat(cachePath(), cache_entry->filename());
  cache_entry->setOnDestroy([file_manager = async_file_manager_, filename]() {
    file_manager->unlink(filename, [filename](absl::Status s) {
      if (!s.ok()) {
        ENVOY_LOG(warn, "file_system_http_cache: failed to unlink {}", filename);
      }
    });
  });
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
