#include "source/extensions/http/cache/file_system_http_cache/insert_context.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_header_proto_util.h"
#include "source/extensions/http/cache/file_system_http_cache/file_system_http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

FileInsertContext::FileInsertContext(std::shared_ptr<FileSystemHttpCache> cache, const Key& key,
                                     const Http::RequestHeaderMap& request_headers,
                                     const VaryAllowList& vary_allow_list)
    : queue_(std::make_shared<InsertOperationQueue>(std::move(cache), key, request_headers,
                                                    vary_allow_list)) {}

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

void InsertOperationQueue::writeChunk(std::shared_ptr<InsertOperationQueue> p,
                                      QueuedFileChunk&& chunk) {
  mu_.AssertHeld();
  auto callback = chunk.done_callback;
  if (chunk.type == QueuedFileChunk::Type::Trailer) {
    size_t sz = chunk.chunk.length();
    cancel_action_in_flight_ =
        file_handle_
            ->write(chunk.chunk, write_pos_,
                    [this, p, sz, callback](absl::StatusOr<size_t> write_result) {
                      absl::MutexLock lock(&mu_);
                      if (!write_result.ok() || write_result.value() != sz) {
                        callback(false);
                        cancelInsert(p, "failed to write trailer chunk");
                        return;
                      }
                      header_block_.setTrailersSize(sz);
                      commit(p, callback);
                    })
            .value();
  } else {
    size_t sz = chunk.chunk.length();
    bool end_stream = chunk.end_stream;
    cancel_action_in_flight_ =
        file_handle_
            ->write(chunk.chunk, write_pos_,
                    [this, p, sz, callback, end_stream](absl::StatusOr<size_t> write_result) {
                      absl::MutexLock lock(&mu_);
                      if (!write_result.ok() || write_result.value() != sz) {
                        callback(false);
                        cancelInsert(p, "failed to write body chunk");
                        return;
                      }
                      write_pos_ += sz;
                      if (end_stream) {
                        commit(p, callback);
                      } else {
                        callback(true);
                        writeNextChunk(p);
                      }
                    })
            .value();
  }
}

void InsertOperationQueue::writeNextChunk(std::shared_ptr<InsertOperationQueue> p) {
  mu_.AssertHeld();
  if (queue_.empty()) {
    cancel_action_in_flight_ = nullptr;
    return;
  }
  auto chunk = std::move(queue_.front());
  queue_.pop_front();
  writeChunk(p, std::move(chunk));
}

void InsertOperationQueue::push(std::shared_ptr<InsertOperationQueue> p, QueuedFileChunk&& chunk) {
  absl::MutexLock lock(&mu_);
  if (!cache_entry_) {
    // Already cancelled, do nothing.
    return;
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
  if (VaryHeaderUtils::hasVary(response_headers)) {
    auto vary_header_values = VaryHeaderUtils::getVaryValues(response_headers);
    Key old_key = key_;
    const auto vary_identifier = VaryHeaderUtils::createVaryIdentifier(
        vary_allow_list_, vary_header_values, request_headers_);
    if (vary_identifier.has_value()) {
      key_.add_custom_fields(vary_identifier.value());
    } else {
      // No error for this cancel, it's just a non-insertable entry.
      cancelInsert(p);
      insert_complete(false);
      return;
    }
    cache_entry_ = cache_->setCacheEntryToVary(old_key, response_headers, key_);
  } else {
    cache_entry_ = cache_->setCacheEntryWorkInProgress(key_);
  }
  if (!cache_entry_) {
    // No error for this cancel, someone else just got there first.
    cancelInsert(p);
    insert_complete(false);
    return;
  }
  CacheFileHeader file_header = protoFromHeadersAndMetadata(key_, response_headers, metadata);
  cache_entry_->setHeaderData(header_block_, file_header);
  // Open the file.
  cancel_action_in_flight_ = cache_entry_->fileManager().createAnonymousFile(
      cache_->cachePath(),
      [this, p, insert_complete, end_stream,
       file_header = std::move(file_header)](absl::StatusOr<AsyncFileHandle> open_result) {
        absl::MutexLock lock(&mu_);
        if (!open_result.ok()) {
          cancelInsert(p, "failed to create anonymous file");
          insert_complete(false);
          return;
        }
        file_handle_ = std::move(open_result.value());
        Buffer::OwnedImpl unset_header(header_block_.stringView());
        // Write an empty header block.
        cancel_action_in_flight_ =
            file_handle_
                ->write(
                    unset_header, 0,
                    [this, p, insert_complete, end_stream,
                     file_header = std::move(file_header)](absl::StatusOr<size_t> write_result) {
                      absl::MutexLock lock(&mu_);
                      if (!write_result.ok() ||
                          write_result.value() != CacheFileFixedBlock::size()) {
                        cancelInsert(p, "failed to write empty header block");
                        insert_complete(false);
                        return;
                      }
                      write_pos_ = header_block_.offsetToBody();
                      if (end_stream) {
                        commit(p, insert_complete);
                        return;
                      }
                      insert_complete(true);
                      writeNextChunk(p);
                    })
                .value();
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
  CacheFileTrailer file_trailer;
  response_trailers.iterate([&file_trailer](const Http::HeaderEntry& trailer) {
    auto t = file_trailer.add_trailers();
    t->set_key(std::string{trailer.key().getStringView()});
    t->set_value(std::string{trailer.value().getStringView()});
    return Http::HeaderMap::Iterate::Continue;
  });
  push(p, QueuedFileChunk{QueuedFileChunk::Type::Trailer, bufferFromProto(file_trailer),
                          insert_complete, true});
}

void InsertOperationQueue::commit(std::shared_ptr<InsertOperationQueue> p,
                                  InsertCallback callback) {
  mu_.AssertHeld();
  // Write the headers at the end of the file.
  auto header_data = cache_entry_->getHeaderData();
  Buffer::OwnedImpl buf = bufferFromProto(header_data.proto);
  size_t sz = buf.length();
  header_block_.setBodySize(write_pos_ - header_block_.offsetToBody());
  header_block_.setHeadersSize(sz);
  cache_entry_->setHeaderData(header_block_, header_data.proto);
  cancel_action_in_flight_ =
      file_handle_
          ->write(buf, header_block_.offsetToHeaders(),
                  [this, callback, p, sz](absl::StatusOr<size_t> write_result) {
                    absl::MutexLock lock(&mu_);
                    if (!write_result.ok() || write_result.value() != sz) {
                      callback(false);
                      cancelInsert(p, "failed to write headers");
                      return;
                    }
                    // Write the file header block.
                    Buffer::OwnedImpl block_buffer(header_block_.stringView());
                    cancel_action_in_flight_ =
                        file_handle_
                            ->write(block_buffer, 0,
                                    [this, callback, p](absl::StatusOr<size_t> write_result) {
                                      absl::MutexLock lock(&mu_);
                                      if (!write_result.ok() ||
                                          write_result.value() != CacheFileFixedBlock::size()) {
                                        callback(false);
                                        cancelInsert(p, "failed to write header block");
                                        return;
                                      }
                                      absl::string_view path = cache_->cachePath();
                                      // Link the file to its filename.
                                      cancel_action_in_flight_ =
                                          file_handle_
                                              ->createHardLink(
                                                  absl::StrCat(path, cache_entry_->filename()),
                                                  [this, callback, p](absl::Status link_result) {
                                                    absl::MutexLock lock(&mu_);
                                                    if (!link_result.ok()) {
                                                      callback(false);
                                                      cancelInsert(
                                                          p,
                                                          absl::StrCat("failed to link file (",
                                                                       link_result.ToString(),
                                                                       "): ", cache_->cachePath(),
                                                                       cache_entry_->filename()));
                                                      return;
                                                    }
                                                    // Mark the cache entry as useable.
                                                    cache_->updateEntryToFile(
                                                        key_, std::move(cache_entry_));
                                                    cancel_action_in_flight_ = nullptr;
                                                    callback(true);
                                                  })
                                              .value();
                                    })
                            .value();
                  })
          .value();
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
  while (!queue_.empty()) {
    auto chunk = std::move(queue_.front());
    queue_.pop_front();
    if (chunk.done_callback) {
      chunk.done_callback(false);
    }
  }
  if (cache_entry_) {
    cache_->removeCacheEntryInProgress(key_);
    cache_entry_ = nullptr;
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