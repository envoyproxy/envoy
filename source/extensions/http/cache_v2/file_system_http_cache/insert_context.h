#pragma once

#include <memory>

#include "source/extensions/common/async_files/async_file_manager.h"
#include "source/extensions/filters/http/cache_v2/http_cache.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/cache_file_fixed_block.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/cache_file_header.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace FileSystemHttpCache {

struct CacheShared;

class FileInsertContext : public Logger::Loggable<Logger::Id::cache_filter> {
public:
  static void begin(Event::Dispatcher& dispatcher, Key key, std::string filepath,
                    Http::ResponseHeaderMapPtr headers, ResponseMetadata metadata,
                    HttpSourcePtr source, std::shared_ptr<CacheProgressReceiver> progress,
                    std::shared_ptr<CacheShared> stat_recorder,
                    Common::AsyncFiles::AsyncFileManager& async_file_manager);

private:
  FileInsertContext(Event::Dispatcher& dispatcher, Key key, std::string filepath,
                    Http::ResponseHeaderMapPtr headers, ResponseMetadata metadata,
                    HttpSourcePtr source, std::shared_ptr<CacheProgressReceiver> progress,
                    std::shared_ptr<CacheShared> stat_recorder);
  void fail(absl::Status status);
  void complete();

  // The sequence of actions involved in writing the cache entry to a file. Each
  // of these actions are posted to an async file thread, and the results posted back
  // to the dispatcher, so the callbacks are run on the original filter's thread.
  // Any failure calls CacheProgressReceiver::onInsertFailed.

  // The first step of writing the cache entry to a file. On success calls
  // dupFile.
  void createFile(Common::AsyncFiles::AsyncFileManager& file_manager);
  // Makes a duplicate file handle for the Reader.
  // On success calls writeEmptyHeaderBlock and CacheProgressReceiver::onHeadersInserted.
  void dupFile();
  // An empty header block is written at the start of the file, making room for
  // a populated header block to be written later. On success calls
  // either getBody or writeHeaders depending on if there is any body.
  void writeEmptyHeaderBlock();
  // Reads a chunk of body for insertion. Calls onBody on success. Calls getTrailers
  // if no body remained and there are trailers, or writeHeaders if no body remained
  // and there are no trailers.
  void getBody();
  // Writes a chunk of body to the file. Calls CacheProgressReceiver::onBodyInserted
  // and getBody, or writeHeaders if body ended and there are no trailers.
  void onBody(Buffer::InstancePtr buf, bool end_stream);
  // Reads trailers. Calls onTrailers on success.
  void getTrailers();
  // Writes the trailers to file. Calls CacheProcessReceiver::onTrailersInserted
  // and writeHeaders on success.
  void onTrailers(Http::ResponseTrailerMapPtr trailers);
  // Writes the headers to file. Calls commit on success.
  void writeHeaders();
  // Rewrites the header block of the file, and calls createHardLink.
  void commit();
  // Creates a hard link, and updates stats.
  void createHardLink();

  Event::Dispatcher& dispatcher_;
  std::string filepath_;
  CacheFileHeader cache_file_header_proto_;
  Http::ResponseHeaderMapPtr headers_;
  HttpSourcePtr source_;
  std::shared_ptr<CacheProgressReceiver> progress_receiver_;
  std::shared_ptr<CacheShared> stat_recorder_;
  CacheFileFixedBlock header_block_;
  Common::AsyncFiles::AsyncFileHandle file_handle_;
  off_t read_pos_{0};
};

} // namespace FileSystemHttpCache
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
