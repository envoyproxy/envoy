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
    : lookup_context_(std::move(lookup_context)), key_(lookup_context_->lookup().key()),
      cache_(std::move(cache)) {}

void FileInsertContext::insertHeaders(const Http::ResponseHeaderMap& response_headers,
                                      const ResponseMetadata& metadata,
                                      InsertCallback insert_complete, bool end_stream) {
  ASSERT(dispatcher()->isThreadSafe());
  callback_in_flight_ = std::move(insert_complete);
  const VaryAllowList& vary_allow_list = lookup_context_->lookup().varyAllowList();
  const Http::RequestHeaderMap& request_headers = lookup_context_->lookup().requestHeaders();
  if (VaryHeaderUtils::hasVary(response_headers)) {
    auto vary_header_values = VaryHeaderUtils::getVaryValues(response_headers);
    Key old_key = key_;
    const auto vary_identifier =
        VaryHeaderUtils::createVaryIdentifier(vary_allow_list, vary_header_values, request_headers);
    if (vary_identifier.has_value()) {
      key_.add_custom_fields(vary_identifier.value());
    } else {
      // No error for this cancel, it's just an entry that's ineligible for insertion.
      cancelInsert();
      return;
    }
    cleanup_ =
        cache_->setCacheEntryToVary(*dispatcher(), old_key, response_headers, key_, cleanup_);
  } else {
    cleanup_ = cache_->maybeStartWritingEntry(key_);
  }
  if (!cleanup_) {
    // No error for this cancel, someone else just got there first.
    cancelInsert();
    return;
  }
  cache_file_header_proto_ = makeCacheFileHeaderProto(key_, response_headers, metadata);
  end_stream_after_headers_ = end_stream;
  createFile();
}

void FileInsertContext::createFile() {
  ASSERT(dispatcher()->isThreadSafe());
  ASSERT(!cancel_action_in_flight_);
  ASSERT(callback_in_flight_ != nullptr);
  cancel_action_in_flight_ = cache_->asyncFileManager()->createAnonymousFile(
      dispatcher(), cache_->cachePath(), [this](absl::StatusOr<AsyncFileHandle> open_result) {
        cancel_action_in_flight_ = nullptr;
        if (!open_result.ok()) {
          cancelInsert("failed to create anonymous file");
          return;
        }
        file_handle_ = std::move(open_result.value());
        writeEmptyHeaderBlock();
      });
}

void FileInsertContext::writeEmptyHeaderBlock() {
  ASSERT(dispatcher()->isThreadSafe());
  ASSERT(!cancel_action_in_flight_);
  ASSERT(callback_in_flight_ != nullptr);
  Buffer::OwnedImpl unset_header;
  header_block_.serializeToBuffer(unset_header);
  // Write an empty header block.
  auto queued = file_handle_->write(
      dispatcher(), unset_header, 0, [this](absl::StatusOr<size_t> write_result) {
        cancel_action_in_flight_ = nullptr;
        if (!write_result.ok() || write_result.value() != CacheFileFixedBlock::size()) {
          cancelInsert(
              writeFailureMessage("empty header block", write_result, CacheFileFixedBlock::size()));
          return;
        }
        writeHeaderProto();
      });
  ASSERT(queued.ok(), queued.status().ToString());
  cancel_action_in_flight_ = std::move(queued.value());
}

void FileInsertContext::succeedCurrentAction() {
  ASSERT(!cancel_action_in_flight_);
  ASSERT(callback_in_flight_ != nullptr);
  auto cb = std::move(callback_in_flight_);
  callback_in_flight_ = nullptr;
  cb(true);
}

void FileInsertContext::writeHeaderProto() {
  ASSERT(dispatcher()->isThreadSafe());
  ASSERT(!cancel_action_in_flight_);
  ASSERT(callback_in_flight_ != nullptr);
  auto buf = bufferFromProto(cache_file_header_proto_);
  auto sz = buf.length();
  auto queued =
      file_handle_->write(dispatcher(), buf, header_block_.offsetToHeaders(),
                          [this, sz](absl::StatusOr<size_t> write_result) {
                            cancel_action_in_flight_ = nullptr;
                            if (!write_result.ok() || write_result.value() != sz) {
                              cancelInsert(writeFailureMessage("headers", write_result, sz));
                              return;
                            }
                            header_block_.setHeadersSize(sz);
                            if (end_stream_after_headers_) {
                              commit();
                              return;
                            }
                            succeedCurrentAction();
                          });
  ASSERT(queued.ok(), queued.status().ToString());
  cancel_action_in_flight_ = std::move(queued.value());
}

void FileInsertContext::insertBody(const Buffer::Instance& fragment,
                                   InsertCallback ready_for_next_fragment, bool end_stream) {
  ASSERT(dispatcher()->isThreadSafe());
  ASSERT(!cancel_action_in_flight_, "should be no actions in flight when receiving new data");
  ASSERT(!callback_in_flight_);
  if (!cleanup_) {
    // Already cancelled, do nothing, return failure.
    std::move(ready_for_next_fragment)(false);
    return;
  }
  callback_in_flight_ = std::move(ready_for_next_fragment);
  size_t sz = fragment.length();
  Buffer::OwnedImpl consumable_fragment(fragment);
  auto queued = file_handle_->write(
      dispatcher(), consumable_fragment, header_block_.offsetToBody() + header_block_.bodySize(),
      [this, sz, end_stream](absl::StatusOr<size_t> write_result) {
        cancel_action_in_flight_ = nullptr;
        if (!write_result.ok() || write_result.value() != sz) {
          cancelInsert(writeFailureMessage("body chunk", write_result, sz));
          return;
        }
        header_block_.setBodySize(header_block_.bodySize() + sz);
        if (end_stream) {
          commit();
        } else {
          succeedCurrentAction();
        }
      });
  ASSERT(queued.ok(), queued.status().ToString());
  cancel_action_in_flight_ = std::move(queued.value());
}

void FileInsertContext::insertTrailers(const Http::ResponseTrailerMap& trailers,
                                       InsertCallback insert_complete) {
  ASSERT(dispatcher()->isThreadSafe());
  ASSERT(!cancel_action_in_flight_, "should be no actions in flight when receiving new data");
  ASSERT(!callback_in_flight_);
  if (!cleanup_) {
    // Already cancelled, do nothing, return failure.
    std::move(insert_complete)(false);
    return;
  }
  callback_in_flight_ = std::move(insert_complete);
  CacheFileTrailer file_trailer = makeCacheFileTrailerProto(trailers);
  Buffer::OwnedImpl consumable_buffer = bufferFromProto(file_trailer);
  size_t sz = consumable_buffer.length();
  auto queued =
      file_handle_->write(dispatcher(), consumable_buffer, header_block_.offsetToTrailers(),
                          [this, sz](absl::StatusOr<size_t> write_result) {
                            cancel_action_in_flight_ = nullptr;
                            if (!write_result.ok() || write_result.value() != sz) {
                              cancelInsert(writeFailureMessage("trailer chunk", write_result, sz));
                              return;
                            }
                            header_block_.setTrailersSize(sz);
                            commit();
                          });
  ASSERT(queued.ok(), queued.status().ToString());
  cancel_action_in_flight_ = std::move(queued.value());
}

void FileInsertContext::onDestroy() {
  lookup_context_->onDestroy();
  cancelInsert("InsertContext destroyed prematurely");
}

void FileInsertContext::commit() {
  ASSERT(dispatcher()->isThreadSafe());
  ASSERT(!cancel_action_in_flight_);
  ASSERT(callback_in_flight_ != nullptr);
  // Write the file header block now that we know the sizes of the pieces.
  Buffer::OwnedImpl block_buffer;
  header_block_.serializeToBuffer(block_buffer);
  auto queued = file_handle_->write(
      dispatcher(), block_buffer, 0, [this](absl::StatusOr<size_t> write_result) {
        cancel_action_in_flight_ = nullptr;
        if (!write_result.ok() || write_result.value() != CacheFileFixedBlock::size()) {
          cancelInsert(
              writeFailureMessage("header block", write_result, CacheFileFixedBlock::size()));
          return;
        }
        commitMeasureExisting();
      });
  ASSERT(queued.ok(), queued.status().ToString());
  cancel_action_in_flight_ = std::move(queued.value());
}

std::string FileInsertContext::pathAndFilename() {
  return absl::StrCat(cache_->cachePath(), cache_->generateFilename(key_));
}

void FileInsertContext::commitMeasureExisting() {
  ASSERT(!cancel_action_in_flight_);
  ASSERT(callback_in_flight_ != nullptr);
  cancel_action_in_flight_ = cache_->asyncFileManager()->stat(
      dispatcher(), pathAndFilename(), [this](absl::StatusOr<struct stat> stat_result) {
        cancel_action_in_flight_ = nullptr;
        if (stat_result.ok()) {
          commitUnlinkExisting(stat_result.value().st_size);
        } else {
          commitUnlinkExisting(0);
        }
      });
}

void FileInsertContext::commitUnlinkExisting(size_t file_size) {
  ASSERT(!cancel_action_in_flight_);
  ASSERT(callback_in_flight_ != nullptr);
  cancel_action_in_flight_ = cache_->asyncFileManager()->unlink(
      dispatcher(), pathAndFilename(), [this, file_size](absl::Status unlink_result) {
        cancel_action_in_flight_ = nullptr;
        if (unlink_result.ok()) {
          cache_->trackFileRemoved(file_size);
        }
        commitCreateHardLink();
      });
}

void FileInsertContext::commitCreateHardLink() {
  ASSERT(!cancel_action_in_flight_);
  ASSERT(callback_in_flight_ != nullptr);
  auto queued = file_handle_->createHardLink(
      dispatcher(), pathAndFilename(), [this](absl::Status link_result) {
        cancel_action_in_flight_ = nullptr;
        if (!link_result.ok()) {
          cancelInsert(absl::StrCat("failed to link file (", link_result.ToString(),
                                    "): ", pathAndFilename()));
          return;
        }
        ENVOY_LOG(debug, "created cache file {}", cache_->generateFilename(key_));
        succeedCurrentAction();
        uint64_t file_size = header_block_.offsetToTrailers() + header_block_.trailerSize();
        cache_->trackFileAdded(file_size);
        // By clearing cleanup before destructor, we prevent logging an error.
        cleanup_ = nullptr;
      });
  ASSERT(queued.ok(), queued.status().ToString());
  cancel_action_in_flight_ = std::move(queued.value());
}

void FileInsertContext::cancelInsert(absl::string_view error) {
  if (cancel_action_in_flight_) {
    cancel_action_in_flight_();
    cancel_action_in_flight_ = nullptr;
  }
  if (callback_in_flight_) {
    callback_in_flight_(false);
    callback_in_flight_ = nullptr;
  }
  if (cleanup_) {
    cleanup_ = nullptr;
    if (!error.empty()) {
      ENVOY_LOG(warn, "FileSystemHttpCache: {}", error);
    }
  }
  if (file_handle_) {
    auto close_status = file_handle_->close(nullptr, [](absl::Status) {});
    ASSERT(close_status.ok());
    file_handle_ = nullptr;
  }
}

Event::Dispatcher* FileInsertContext::dispatcher() const { return lookup_context_->dispatcher(); }

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
