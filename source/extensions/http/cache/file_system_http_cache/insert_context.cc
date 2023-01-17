#include "source/extensions/http/cache/file_system_http_cache/insert_context.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_header_proto_util.h"
#include "source/extensions/http/cache/file_system_http_cache/file_system_http_cache.h"
#include "source/extensions/http/cache/file_system_http_cache/lookup_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

namespace {
std::string writeFailureMessage(absl::string_view kind, absl::StatusOr<size_t> result,
                                size_t wanted) {
  if (result.ok()) {
    return fmt::format("incomplete write of {} - wrote {}, expected {}", kind, result.value(),
                       wanted);
  } else {
    return fmt::format("write failed of {}: {}", kind, result.status());
  }
}
} // namespace

FileInsertContext::FileInsertContext(std::shared_ptr<FileSystemHttpCache> cache,
                                     std::unique_ptr<FileLookupContext> lookup_context)
    : lookup_context_(std::move(lookup_context)),
      queue_(std::make_shared<InsertOperationQueue>(
          std::move(cache), lookup_context_->lookup().key(),
          lookup_context_->lookup().requestHeaders(), lookup_context_->lookup().varyAllowList())) {}

void FileInsertContext::insertHeaders(const Http::ResponseHeaderMap& response_headers,
                                      const ResponseMetadata& metadata,
                                      InsertCallback insert_complete, bool end_stream) {
  queue_->insertHeaders(queue_, response_headers, metadata, insert_complete, end_stream);
}

void FileInsertContext::insertBody(const Buffer::Instance& chunk,
                                   InsertCallback ready_for_next_chunk, bool end_stream) {
  queue_->insertBody(queue_, chunk, ready_for_next_chunk, end_stream);
}

void FileInsertContext::insertTrailers(const Http::ResponseTrailerMap& trailers,
                                       InsertCallback insert_complete) {
  queue_->insertTrailers(queue_, trailers, insert_complete);
}

void FileInsertContext::onDestroy() { queue_->cancelIfIncomplete(queue_); }

void InsertOperationQueue::cancelIfIncomplete(std::shared_ptr<InsertOperationQueue> p) {
  absl::MutexLock lock(&mu_);
  if (seen_end_stream_) {
    return;
  }
  cancelInsert(p, "InsertContext destroyed prematurely");
}

void InsertOperationQueue::writeChunk(std::shared_ptr<InsertOperationQueue> p,
                                      QueuedFileChunk&& chunk) {
  mu_.AssertHeld();
  callback_in_flight_ = chunk.done_callback;
  if (chunk.type == QueuedFileChunk::Type::Trailer) {
    size_t sz = chunk.chunk.length();
    auto queued = file_handle_->write(
        chunk.chunk, header_block_.offsetToTrailers(),
        [this, p, sz](absl::StatusOr<size_t> write_result) {
          absl::MutexLock lock(&mu_);
          cancel_action_in_flight_ = nullptr;
          if (!write_result.ok() || write_result.value() != sz) {
            cancelInsert(p, writeFailureMessage("trailer chunk", write_result, sz));
            return;
          }
          header_block_.setTrailersSize(sz);
          commit(p, callback_in_flight_);
        });
    ASSERT(queued.ok(), queued.status().ToString());
    cancel_action_in_flight_ = queued.value();
  } else {
    size_t sz = chunk.chunk.length();
    bool end_stream = chunk.end_stream;
    auto queued = file_handle_->write(
        chunk.chunk, header_block_.offsetToBody() + header_block_.bodySize(),
        [this, p, sz, end_stream](absl::StatusOr<size_t> write_result) {
          absl::MutexLock lock(&mu_);
          cancel_action_in_flight_ = nullptr;
          if (!write_result.ok() || write_result.value() != sz) {
            cancelInsert(p, writeFailureMessage("body chunk", write_result, sz));
            return;
          }
          header_block_.setBodySize(header_block_.bodySize() + sz);
          if (end_stream) {
            commit(p, callback_in_flight_);
          } else {
            callback_in_flight_(true);
            callback_in_flight_ = nullptr;
            writeNextChunk(p);
          }
        });
    ASSERT(queued.ok(), queued.status().ToString());
    cancel_action_in_flight_ = queued.value();
  }
}

void InsertOperationQueue::writeNextChunk(std::shared_ptr<InsertOperationQueue> p) {
  mu_.AssertHeld();
  if (queue_.empty()) {
    ASSERT(cancel_action_in_flight_ == nullptr);
    return;
  }
  auto chunk = std::move(queue_.front());
  queue_.pop_front();
  writeChunk(p, std::move(chunk));
}

void InsertOperationQueue::push(std::shared_ptr<InsertOperationQueue> p, QueuedFileChunk&& chunk) {
  absl::MutexLock lock(&mu_);
  if (!cleanup_) {
    // Already cancelled, do nothing, return failure.
    chunk.done_callback(false);
    return;
  }
  if (chunk.end_stream) {
    seen_end_stream_ = true;
  }
  if (!cancel_action_in_flight_) {
    writeChunk(p, std::move(chunk));
  } else {
    queue_.push_back(std::move(chunk));
  }
}

void InsertOperationQueue::insertHeaders(std::shared_ptr<InsertOperationQueue> p,
                                         const Envoy::Http::ResponseHeaderMap& response_headers,
                                         const ResponseMetadata& metadata,
                                         InsertCallback insert_complete, bool end_stream) {
  absl::MutexLock lock(&mu_);
  callback_in_flight_ = insert_complete;
  if (VaryHeaderUtils::hasVary(response_headers)) {
    auto vary_header_values = VaryHeaderUtils::getVaryValues(response_headers);
    Key old_key = key_;
    const auto vary_identifier = VaryHeaderUtils::createVaryIdentifier(
        vary_allow_list_, vary_header_values, request_headers_);
    if (vary_identifier.has_value()) {
      key_.add_custom_fields(vary_identifier.value());
    } else {
      // No error for this cancel, it's just an entry that's ineligible for insertion.
      cancelInsert(p);
      return;
    }
    cleanup_ = cache_->setCacheEntryToVary(old_key, response_headers, key_, cleanup_);
  } else {
    cleanup_ = cache_->maybeStartWritingEntry(key_);
  }
  if (!cleanup_) {
    // No error for this cancel, someone else just got there first.
    cancelInsert(p);
    return;
  }
  auto header_proto = makeCacheFileHeaderProto(key_, response_headers, metadata);
  // Open the file.
  cancel_action_in_flight_ = cache_->asyncFileManager()->createAnonymousFile(
      cache_->cachePath(),
      [this, p, end_stream, header_proto](absl::StatusOr<AsyncFileHandle> open_result) {
        absl::MutexLock lock(&mu_);
        cancel_action_in_flight_ = nullptr;
        if (!open_result.ok()) {
          cancelInsert(p, "failed to create anonymous file");
          return;
        }
        file_handle_ = std::move(open_result.value());
        Buffer::OwnedImpl unset_header;
        header_block_.serializeToBuffer(unset_header);
        // Write an empty header block.
        auto queued = file_handle_->write(
            unset_header, 0,
            [this, p, end_stream, header_proto](absl::StatusOr<size_t> write_result) {
              absl::MutexLock lock(&mu_);
              cancel_action_in_flight_ = nullptr;
              if (!write_result.ok() || write_result.value() != CacheFileFixedBlock::size()) {
                cancelInsert(p, writeFailureMessage("empty header block", write_result,
                                                    CacheFileFixedBlock::size()));
                return;
              }
              auto buf = bufferFromProto(header_proto);
              auto sz = buf.length();
              auto queued = file_handle_->write(
                  buf, header_block_.offsetToHeaders(),
                  [this, p, end_stream, sz](absl::StatusOr<size_t> write_result) {
                    absl::MutexLock lock(&mu_);
                    cancel_action_in_flight_ = nullptr;
                    if (!write_result.ok() || write_result.value() != sz) {
                      cancelInsert(p, writeFailureMessage("headers", write_result, sz));
                      return;
                    }
                    header_block_.setHeadersSize(sz);
                    if (end_stream) {
                      commit(p, callback_in_flight_);
                      return;
                    }
                    callback_in_flight_(true);
                    callback_in_flight_ = nullptr;
                    writeNextChunk(p);
                  });
              ASSERT(queued.ok(), queued.status().ToString());
              cancel_action_in_flight_ = queued.value();
            });
        ASSERT(queued.ok(), queued.status().ToString());
        cancel_action_in_flight_ = queued.value();
      });
}

void InsertOperationQueue::insertBody(std::shared_ptr<InsertOperationQueue> p,
                                      const Buffer::Instance& chunk,
                                      InsertCallback ready_for_next_chunk, bool end_stream) {
  push(p, QueuedFileChunk{QueuedFileChunk::Type::Body, chunk, ready_for_next_chunk, end_stream});
}

void InsertOperationQueue::insertTrailers(std::shared_ptr<InsertOperationQueue> p,
                                          const Http::ResponseTrailerMap& response_trailers,
                                          InsertCallback insert_complete) {
  CacheFileTrailer file_trailer = makeCacheFileTrailerProto(response_trailers);
  push(p, QueuedFileChunk{QueuedFileChunk::Type::Trailer, bufferFromProto(file_trailer),
                          insert_complete, true});
}

void InsertOperationQueue::commit(std::shared_ptr<InsertOperationQueue> p,
                                  InsertCallback callback) {
  mu_.AssertHeld();
  // Write the file header block now that we know the sizes of the pieces.
  Buffer::OwnedImpl block_buffer;
  callback_in_flight_ = callback;
  header_block_.serializeToBuffer(block_buffer);
  auto queued =
      file_handle_->write(block_buffer, 0, [this, p](absl::StatusOr<size_t> write_result) {
        absl::MutexLock lock(&mu_);
        cancel_action_in_flight_ = nullptr;
        if (!write_result.ok() || write_result.value() != CacheFileFixedBlock::size()) {
          cancelInsert(
              p, writeFailureMessage("header block", write_result, CacheFileFixedBlock::size()));
          return;
        }
        // Unlink any existing cache entry with this filename.
        cancel_action_in_flight_ = cache_->asyncFileManager()->stat(
            absl::StrCat(cache_->cachePath(), cache_->generateFilename(key_)),
            [this, p](absl::StatusOr<struct stat> stat_result) {
              absl::MutexLock lock(&mu_);
              cancel_action_in_flight_ = nullptr;
              size_t file_size = 0;
              if (stat_result.ok()) {
                file_size = stat_result.value().st_size;
              }
              cancel_action_in_flight_ = cache_->asyncFileManager()->unlink(
                  absl::StrCat(cache_->cachePath(), cache_->generateFilename(key_)),
                  [this, file_size, p](absl::Status unlink_result) {
                    if (unlink_result.ok()) {
                      cache_->trackFileRemoved(file_size);
                    }
                    // We can ignore failure of unlink - the file may or may not have previously
                    // existed.
                    absl::MutexLock lock(&mu_);
                    cancel_action_in_flight_ = nullptr;
                    // Link the file to its filename.
                    auto queued = file_handle_->createHardLink(
                        absl::StrCat(cache_->cachePath(), cache_->generateFilename(key_)),
                        [this, p](absl::Status link_result) {
                          absl::MutexLock lock(&mu_);
                          cancel_action_in_flight_ = nullptr;
                          if (!link_result.ok()) {
                            cancelInsert(p, absl::StrCat("failed to link file (",
                                                         link_result.ToString(),
                                                         "): ", cache_->cachePath(),
                                                         cache_->generateFilename(key_)));
                            return;
                          }
                          ENVOY_LOG(debug, "created cache file {}", cache_->generateFilename(key_));
                          callback_in_flight_(true);
                          callback_in_flight_ = nullptr;
                          uint64_t file_size =
                              header_block_.offsetToTrailers() + header_block_.trailerSize();
                          cache_->trackFileAdded(file_size);
                          // By clearing cleanup before destructor, we prevent logging an error.
                          cleanup_ = nullptr;
                        });
                    ASSERT(queued.ok(), queued.status().ToString());
                    cancel_action_in_flight_ = queued.value();
                  });
            });
      });
  ASSERT(queued.ok(), queued.status().ToString());
  cancel_action_in_flight_ = queued.value();
}

InsertOperationQueue::~InsertOperationQueue() {
  absl::MutexLock lock(&mu_);
  cancelInsert(nullptr, "destroyed before completion");
}

void InsertOperationQueue::cancelInsert(std::shared_ptr<InsertOperationQueue>,
                                        absl::string_view error) {
  mu_.AssertHeld();
  if (cancel_action_in_flight_) {
    cancel_action_in_flight_();
    cancel_action_in_flight_ = nullptr;
  }
  if (callback_in_flight_) {
    callback_in_flight_(false);
    callback_in_flight_ = nullptr;
  }
  while (!queue_.empty()) {
    auto chunk = std::move(queue_.front());
    queue_.pop_front();
    if (chunk.done_callback) {
      chunk.done_callback(false);
    }
  }
  if (cleanup_) {
    cleanup_ = nullptr;
    if (!error.empty()) {
      ENVOY_LOG(warn, "FileSystemHttpCache: {}", error);
    }
  }
  if (file_handle_) {
    auto close_status = file_handle_->close([](absl::Status) {});
    ASSERT(close_status.ok());
    file_handle_ = nullptr;
  }
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
