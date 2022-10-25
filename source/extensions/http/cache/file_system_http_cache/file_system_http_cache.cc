#include "source/extensions/http/cache/file_system_http_cache/file_system_http_cache.h"

#include "source/extensions/http/cache/file_system_http_cache/cache_entry_file.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_fixed_block.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_header_proto_util.h"
#include "source/extensions/http/cache/file_system_http_cache/insert_context.h"
#include "source/extensions/http/cache/file_system_http_cache/lookup_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

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
        Buffer::OwnedImpl buf2{block.stringView()};
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
    Singleton::InstanceSharedPtr owner, ConfigProto config,
    std::shared_ptr<Common::AsyncFiles::AsyncFileManager>&& async_file_manager)
    : owner_(owner), config_(config), async_file_manager_(async_file_manager) {}

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
  absl::MutexLock lock(&cache_mu_);
  bool work_in_progress = entries_being_written_.contains(lookup.key());
  return std::make_unique<FileLookupContext>(*this, std::move(lookup), work_in_progress);
}

// Helper class to reduce the lambda depth of updateHeaders.
class HeaderUpdateContext : public Logger::Loggable<Logger::Id::cache_filter> {
public:
  HeaderUpdateContext(const FileSystemHttpCache& cache, const Key& key,
                      std::shared_ptr<Cleanup> cleanup,
                      const Http::ResponseHeaderMap& response_headers,
                      const ResponseMetadata& metadata)
      : filepath_(absl::StrCat(cache.cachePath(), cache.generateFilename(key))),
        cache_path_(cache.cachePath()), cleanup_(cleanup),
        async_file_manager_(cache.asyncFileManager()),
        response_(protoFromHeadersAndMetadata(key, response_headers, metadata)) {}

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
    if (read_handle_ && write_handle_) {
      read_handle_
          ->close([write_handle = write_handle_](absl::Status) {
            write_handle->close([](absl::Status) {}).IgnoreError();
          })
          .IgnoreError();
    } else if (read_handle_) {
      read_handle_->close([](absl::Status) {}).IgnoreError();
    } else if (write_handle_) {
      write_handle_->close([](absl::Status) {}).IgnoreError();
    }
  }

private:
  void unlinkOriginal(std::shared_ptr<HeaderUpdateContext> ctx) {
    async_file_manager_->unlink(filepath_, [ctx, this](absl::Status unlink_result) {
      if (!unlink_result.ok()) {
        fail("unlink failed", unlink_result);
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
          header_proto_ = headerProtoFromBuffer(*read_result.value());
          if (header_proto_.headers_size() == 1 && header_proto_.headers(0).key() == "vary") {
            // TODO(ravenblack): do we need to handle vary entries here? How
            // did we get to updateHeaders on a vary entry? Just abort for now.
            // (The entry was deleted at this point, so we should eventually get
            // back into a useable state even if this is a valid event. The
            // example cache implementation punts on this.)
            fail("not implemented updating vary header", absl::OkStatus());
            return;
          }
          updateProtoFromHeadersAndMetadata(header_proto_, response_);
          header_block_.setHeadersSize(headerProtoSize(header_proto_));
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
          writeHeaderBlock(ctx);
        });
  }
  void writeHeaderBlock(std::shared_ptr<HeaderUpdateContext> ctx) {
    Buffer::OwnedImpl buf{header_block_.stringView()};
    auto queued = write_handle_->write(buf, 0, [ctx, this](absl::StatusOr<size_t> write_result) {
      if (!write_result.ok() || write_result.value() != CacheFileFixedBlock::size()) {
        fail("failed to write header block", write_result.status());
        return;
      }
      writeBody(ctx, CacheFileFixedBlock::size());
    });
    ASSERT(queued.ok());
  }
  void writeBody(std::shared_ptr<HeaderUpdateContext> ctx, off_t offset) {
    size_t sz = header_block_.offsetToHeaders() - offset;
    if (sz == 0) {
      writeHeaders(ctx);
      return;
    }
    static const size_t max_copy_chunk = 128 * 1024;
    sz = std::min(sz, max_copy_chunk);
    auto queued = read_handle_->read(
        offset, sz, [ctx, offset, sz, this](absl::StatusOr<Buffer::InstancePtr> read_result) {
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
                                     writeBody(ctx, offset + sz);
                                   });
          ASSERT(queued.ok());
        });
    ASSERT(queued.ok());
  }
  void writeHeaders(std::shared_ptr<HeaderUpdateContext> ctx) {
    auto buf = bufferFromProto(header_proto_);
    size_t sz = buf.length();
    auto queued = write_handle_->write(buf, header_block_.offsetToHeaders(),
                                       [ctx, sz, this](absl::StatusOr<size_t> write_result) {
                                         if (!write_result.ok() || write_result.value() != sz) {
                                           fail("failed to write headers", write_result.status());
                                           return;
                                         }
                                         linkNewFile(ctx);
                                       });
    ASSERT(queued.ok());
  }
  void linkNewFile(std::shared_ptr<HeaderUpdateContext> ctx) {
    auto queued = write_handle_->createHardLink(filepath_, [ctx, this](absl::Status link_result) {
      if (!link_result.ok()) {
        fail("failed to link new cache file", link_result);
      }
    });
    ASSERT(queued.ok());
  }
  void fail(absl::string_view msg, absl::Status status) {
    ENVOY_LOG(warn, "file_system_http_cache: {} for update cache file {}: {}", msg, filepath_,
              status);
  }
  std::string filepath_;
  std::string cache_path_;
  std::shared_ptr<Cleanup> cleanup_;
  std::shared_ptr<Common::AsyncFiles::AsyncFileManager> async_file_manager_;
  CacheFileHeader response_;
  CacheFileFixedBlock header_block_;
  CacheFileHeader header_proto_;
  AsyncFileHandle read_handle_;
  AsyncFileHandle write_handle_;
};

void FileSystemHttpCache::updateHeaders(const LookupContext& lookup_context,
                                        const Http::ResponseHeaderMap& response_headers,
                                        const ResponseMetadata& metadata) {
  const Key& key = dynamic_cast<const FileLookupContext&>(lookup_context).key();
  auto cleanup = maybeStartWritingEntry(key);
  if (!cleanup) {
    return;
  }
  auto ctx = std::make_shared<HeaderUpdateContext>(*this, key, cleanup, response_headers, metadata);
  ctx->begin(ctx);
}

absl::string_view FileSystemHttpCache::cachePath() const { return config_.cache_path(); }

std::shared_ptr<Cleanup> FileSystemHttpCache::maybeStartWritingEntry(const Key& key) {
  absl::MutexLock lock(&cache_mu_);
  if (!entries_being_written_.emplace(key).second) {
    return nullptr;
  }
  operations_in_flight_++;
  return std::make_shared<Cleanup>([this, key]() {
    absl::MutexLock lock(&cache_mu_);
    entries_being_written_.erase(key);
    operations_in_flight_--;
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

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
