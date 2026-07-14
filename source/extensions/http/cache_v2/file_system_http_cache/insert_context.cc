#include "source/extensions/http/cache_v2/file_system_http_cache/insert_context.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/cache_file_header_proto_util.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/cache_file_reader.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/file_system_http_cache.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/lookup_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace FileSystemHttpCache {

// Arbitrary 128K fragments to balance memory usage and speed.
static constexpr size_t MaxInsertFragmentSize = 128 * 1024;

using Common::AsyncFiles::AsyncFileHandle;
using Common::AsyncFiles::AsyncFileManager;

void FileInsertContext::begin(Event::Dispatcher& dispatcher, Key key, std::string filepath,
                              Http::ResponseHeaderMapPtr headers, ResponseMetadata metadata,
                              HttpSourcePtr source, std::shared_ptr<CacheProgressReceiver> progress,
                              std::shared_ptr<CacheShared> stat_recorder,
                              AsyncFileManager& file_manager) {
  auto p = new FileInsertContext(dispatcher, std::move(key), std::move(filepath),
                                 std::move(headers), std::move(metadata), std::move(source),
                                 std::move(progress), std::move(stat_recorder));
  p->createFile(file_manager);
}

FileInsertContext::FileInsertContext(Event::Dispatcher& dispatcher, Key key, std::string filepath,
                                     Http::ResponseHeaderMapPtr headers, ResponseMetadata metadata,
                                     HttpSourcePtr source,
                                     std::shared_ptr<CacheProgressReceiver> progress,
                                     std::shared_ptr<CacheShared> stat_recorder)
    : dispatcher_(dispatcher), filepath_(std::move(filepath)),
      cache_file_header_proto_(makeCacheFileHeaderProto(key, *headers, metadata)),
      headers_(std::move(headers)), source_(std::move(source)),
      progress_receiver_(std::move(progress)), stat_recorder_(std::move(stat_recorder)) {}

void FileInsertContext::fail(absl::Status status) {
  progress_receiver_->onInsertFailed(status);
  if (file_handle_) {
    auto queued = file_handle_->close(nullptr, [](absl::Status) {});
    ASSERT(queued.ok());
  }
  delete this;
}

void FileInsertContext::complete() {
  auto queued = file_handle_->close(nullptr, [](absl::Status) {});
  ASSERT(queued.ok());
  delete this;
}

void FileInsertContext::createFile(AsyncFileManager& file_manager) {
  absl::string_view cache_path = absl::string_view{filepath_};
  cache_path = absl::string_view{cache_path.begin(), cache_path.rfind('/') + 1};
  file_manager.createAnonymousFile(
      &dispatcher_, cache_path, [this](absl::StatusOr<AsyncFileHandle> open_result) -> void {
        if (!open_result.ok()) {
          return fail(
              absl::Status(open_result.status().code(),
                           fmt::format("create file failed: {}", open_result.status().message())));
        }
        file_handle_ = std::move(open_result.value());
        dupFile();
      });
}

void FileInsertContext::dupFile() {
  auto queued =
      file_handle_->duplicate(&dispatcher_, [this](absl::StatusOr<AsyncFileHandle> dup_result) {
        if (!dup_result.ok()) {
          return fail(
              absl::Status(dup_result.status().code(), fmt::format("duplicate file failed: {}",
                                                                   dup_result.status().message())));
        }
        bool end_stream = source_ == nullptr;
        progress_receiver_->onHeadersInserted(
            std::make_unique<CacheFileReader>(std::move(dup_result.value())), std::move(headers_),
            end_stream);
        writeEmptyHeaderBlock();
      });
  ASSERT(queued.ok(), queued.status().ToString());
}

void FileInsertContext::writeEmptyHeaderBlock() {
  Buffer::OwnedImpl unset_header;
  header_block_.serializeToBuffer(unset_header);
  // Write an empty header block.
  auto queued = file_handle_->write(
      &dispatcher_, unset_header, 0, [this](absl::StatusOr<size_t> write_result) {
        if (!write_result.ok()) {
          return fail(absl::Status(
              write_result.status().code(),
              fmt::format("write to file failed: {}", write_result.status().message())));
        } else if (write_result.value() != CacheFileFixedBlock::size()) {
          return fail(absl::UnavailableError(
              fmt::format("write to file failed; wrote {} bytes instead of {}",
                          write_result.value(), CacheFileFixedBlock::size())));
        }
        if (source_) {
          getBody();
        } else {
          writeHeaders();
        }
      });
  ASSERT(queued.ok(), queued.status().ToString());
}

void FileInsertContext::getBody() {
  ASSERT(source_);
  source_->getBody(AdjustedByteRange(read_pos_, read_pos_ + MaxInsertFragmentSize),
                   [this](Buffer::InstancePtr buf, EndStream end_stream) {
                     if (end_stream == EndStream::Reset) {
                       return fail(
                           absl::UnavailableError("cache write failed due to upstream reset"));
                     }
                     if (buf == nullptr) {
                       if (end_stream == EndStream::End) {
                         progress_receiver_->onBodyInserted(AdjustedByteRange(0, read_pos_), true);
                         writeHeaders();
                       } else {
                         getTrailers();
                       }
                     } else {
                       read_pos_ += buf->length();
                       onBody(std::move(buf), end_stream == EndStream::End);
                     }
                   });
}

void FileInsertContext::onBody(Buffer::InstancePtr buf, bool end_stream) {
  ASSERT(buf);
  size_t len = buf->length();
  auto queued = file_handle_->write(
      &dispatcher_, *buf, header_block_.offsetToBody() + header_block_.bodySize(),
      [this, len, end_stream](absl::StatusOr<size_t> write_result) {
        if (!write_result.ok()) {
          return fail(absl::Status(
              write_result.status().code(),
              fmt::format("write to file failed: {}", write_result.status().message())));
        } else if (write_result.value() != len) {
          return fail(absl::UnavailableError(fmt::format(
              "write to file failed: wrote {} bytes instead of {}", write_result.value(), len)));
        }
        progress_receiver_->onBodyInserted(
            AdjustedByteRange(header_block_.bodySize(), header_block_.bodySize() + len),
            end_stream);
        header_block_.setBodySize(header_block_.bodySize() + len);
        if (end_stream) {
          writeHeaders();
        } else {
          getBody();
        }
      });
  ASSERT(queued.ok(), queued.status().ToString());
}

void FileInsertContext::getTrailers() {
  source_->getTrailers([this](Http::ResponseTrailerMapPtr trailers, EndStream end_stream) {
    if (end_stream == EndStream::Reset) {
      return fail(
          absl::UnavailableError("write to cache failed, upstream reset during getTrailers"));
    }
    onTrailers(std::move(trailers));
  });
}

void FileInsertContext::onTrailers(Http::ResponseTrailerMapPtr trailers) {
  CacheFileTrailer trailer_proto = makeCacheFileTrailerProto(*trailers);
  progress_receiver_->onTrailersInserted(std::move(trailers));
  Buffer::OwnedImpl trailer_buffer = bufferFromProto(trailer_proto);
  header_block_.setTrailersSize(trailer_buffer.length());
  auto queued = file_handle_->write(&dispatcher_, trailer_buffer, header_block_.offsetToTrailers(),
                                    [this](absl::StatusOr<size_t> write_result) {
                                      if (!write_result.ok() ||
                                          write_result.value() != header_block_.trailerSize()) {
                                        // We've already told the client that the write worked, and
                                        // it already has the data they need, so we can act like it
                                        // was complete until the next lookup, even though the file
                                        // didn't actually get linked.
                                        return complete();
                                      }
                                      writeHeaders();
                                    });
  ASSERT(queued.ok(), queued.status().ToString());
}

void FileInsertContext::writeHeaders() {
  Buffer::OwnedImpl header_buffer = bufferFromProto(cache_file_header_proto_);
  header_block_.setHeadersSize(header_buffer.length());
  auto queued = file_handle_->write(&dispatcher_, header_buffer, header_block_.offsetToHeaders(),
                                    [this](absl::StatusOr<size_t> write_result) {
                                      if (!write_result.ok() ||
                                          write_result.value() != header_block_.headerSize()) {
                                        // We've already told the client that the write worked, and
                                        // it already has the data they need, so we can act like it
                                        // was complete until the next lookup, even though the file
                                        // didn't actually get linked.
                                        return complete();
                                      }
                                      commit();
                                    });
  ASSERT(queued.ok(), queued.status().ToString());
}

void FileInsertContext::commit() {
  // now that the header block knows the size of all the pieces, overwrite it in the file.
  Buffer::OwnedImpl block_buffer;
  header_block_.serializeToBuffer(block_buffer);
  auto queued = file_handle_->write(
      &dispatcher_, block_buffer, 0, [this](absl::StatusOr<size_t> write_result) {
        if (!write_result.ok() || write_result.value() != CacheFileFixedBlock::size()) {
          // We've already told the client that the write worked, and it already
          // has the data they need, so we can act like it was complete until
          // the next lookup, even though the file didn't actually get linked.
          return complete();
        }
        createHardLink();
      });
  ASSERT(queued.ok(), queued.status().ToString());
}

void FileInsertContext::createHardLink() {
  auto queued =
      file_handle_->createHardLink(&dispatcher_, filepath_, [this](absl::Status link_result) {
        if (!link_result.ok()) {
          ENVOY_LOG(error, "failed to link file {}: {}", filepath_, link_result);
          return complete();
        }
        ENVOY_LOG(debug, "created cache file {}", filepath_);
        uint64_t file_size = header_block_.offsetToTrailers() + header_block_.trailerSize();
        stat_recorder_->trackFileAdded(file_size);
        complete();
      });
  ASSERT(queued.ok(), queued.status().ToString());
}

} // namespace FileSystemHttpCache
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
