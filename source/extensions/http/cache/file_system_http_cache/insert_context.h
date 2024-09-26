#pragma once

#include <memory>

#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/filters/http/cache/http_cache.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_fixed_block.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_header.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

using ::Envoy::Extensions::Common::AsyncFiles::AsyncFileHandle;
using ::Envoy::Extensions::Common::AsyncFiles::CancelFunction;

class FileLookupContext;
class FileSystemHttpCache;

class DontInsertContext : public InsertContext {
public:
  explicit DontInsertContext(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}
  void insertHeaders(const Http::ResponseHeaderMap&, const ResponseMetadata&,
                     InsertCallback insert_complete, bool) override {
    dispatcher_.post([cb = std::move(insert_complete)]() mutable { cb(false); });
  }
  void insertBody(const Buffer::Instance&, InsertCallback ready_for_next_chunk, bool) override {
    dispatcher_.post([cb = std::move(ready_for_next_chunk)]() mutable { cb(false); });
  }
  void insertTrailers(const Http::ResponseTrailerMap&, InsertCallback insert_complete) override {
    dispatcher_.post([cb = std::move(insert_complete)]() mutable { cb(false); });
  }
  void onDestroy() override{};

private:
  Event::Dispatcher& dispatcher_;
};

class FileInsertContext : public InsertContext, public Logger::Loggable<Logger::Id::cache_filter> {
public:
  FileInsertContext(std::shared_ptr<FileSystemHttpCache> cache,
                    std::unique_ptr<FileLookupContext> lookup_context);
  void insertHeaders(const Http::ResponseHeaderMap& response_headers,
                     const ResponseMetadata& metadata, InsertCallback insert_complete,
                     bool end_stream) override;
  void insertBody(const Buffer::Instance& chunk, InsertCallback ready_for_next_chunk,
                  bool end_stream) override;
  void insertTrailers(const Http::ResponseTrailerMap& trailers,
                      InsertCallback insert_complete) override;
  void onDestroy() override;

private:
  Event::Dispatcher* dispatcher() const;
  // The sequence of actions involved in writing the cache entry to a file. Each
  // of these actions are posted to an async file thread, and the results posted back
  // to the dispatcher, so the callbacks are run on the original filter's thread.

  // The first step of writing the cache entry to a file. On success calls
  // writeEmptyHeaderBlock, on failure calls the InsertCallback with false which
  // should abort the operation.
  void createFile();
  // An empty header block is written at the start of the file, making room for
  // a populated header block to be written later. On success calls writeHeaderProto,
  // on failure calls the InsertCallback with false which should abort the operation.
  void writeEmptyHeaderBlock();
  // Writes the http headers and updates the headers size in the in-memory header_block_.
  // On success and end_stream, calls commit. On success and not end_stream, calls the
  // InsertCallback with true to move on to the data section. On failure calls the
  // InsertCallback with false which should abort the operation.
  void writeHeaderProto();
  // Helper to call the InsertCallback with true.
  void succeedCurrentAction();
  // Returns the full path for the cache file matching key_.
  std::string pathAndFilename();
  // Starts the commit process; rewrites the header block of the current file. On
  // success calls commitMeasureExisting. On failure calls the InsertCallback with false
  // which should abort the operation.
  void commit();
  // Checks for the presence and size of a pre-existing cache entry at the destination
  // path. If the file does not exist or on failure, calls commitUnlinkExisting, as it
  // doesn't hurt to try the delete in case the stat failure was e.g. a lack of read
  // permission. On success passes the size from stat, on failure passes 0.
  void commitMeasureExisting();
  // Deletes the pre-existing file in the pathAndFilename() location. On success updates
  // cache metrics with the measured size. Regardless of success calls commitCreateHardLink.
  void commitUnlinkExisting(size_t file_size);
  // Creates a hard link at pathAndFilename() to the current file. On success calls
  // InsertCallback with true. On failure calls it with false which should abort the
  // operation.
  void commitCreateHardLink();
  CacheFileHeader cache_file_header_proto_;
  bool end_stream_after_headers_ = false;
  std::unique_ptr<FileLookupContext> lookup_context_;
  Key key_;
  std::shared_ptr<FileSystemHttpCache> cache_;
  std::shared_ptr<Cleanup> cleanup_;
  AsyncFileHandle file_handle_;
  absl::AnyInvocable<void(bool)> callback_in_flight_;
  CancelFunction cancel_action_in_flight_;
  CacheFileFixedBlock header_block_;

  /**
   * If seen_end_stream_ is not true (i.e. InsertContext has not yet delivered the
   * entire response), cancel insertion. Called by InsertContext onDestroy.
   */
  void cancelIfIncomplete();

  /**
   * Cancels any action in flight, calls any uncalled completion callbacks with false,
   * and closes the file if open.
   * @param err a string to log with the failure.
   */
  void cancelInsert(absl::string_view err = "");
};

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
