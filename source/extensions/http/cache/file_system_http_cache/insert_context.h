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
  void insertHeaders(const Http::ResponseHeaderMap&, const ResponseMetadata&,
                     InsertCallback insert_complete, bool) override {
    insert_complete(false);
  }
  void insertBody(const Buffer::Instance&, InsertCallback ready_for_next_chunk, bool) override {
    ready_for_next_chunk(false);
  }
  void insertTrailers(const Http::ResponseTrailerMap&, InsertCallback insert_complete) override {
    insert_complete(false);
  }
  void onDestroy() override{};
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
  std::unique_ptr<FileLookupContext> lookup_context_;
  Key key_;
  std::shared_ptr<FileSystemHttpCache> cache_;
  absl::Mutex mu_; // guards file operations
  std::shared_ptr<Cleanup> cleanup_ ABSL_GUARDED_BY(mu_);
  AsyncFileHandle file_handle_ ABSL_GUARDED_BY(mu_);
  std::function<void(bool)> callback_in_flight_ ABSL_GUARDED_BY(mu_);
  CancelFunction cancel_action_in_flight_ ABSL_GUARDED_BY(mu_);
  CacheFileFixedBlock header_block_ ABSL_GUARDED_BY(mu_);

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
  void cancelInsert(absl::string_view err = "") ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  /**
   * Starts asynchronously performing the final write operations for the cache file;
   * writing the correct FixedCacheHeaderBlock, and giving the file its name.
   * On success, removes cleanup_, so the subsequent call to cancelInsert during
   * the destructor does not act as an error.
   * @param p a shared_ptr to 'this', so it can be captured in lambdas to ensure
   *     'this' still exists when the lambda is called.
   * @param callback is called with true if the commit completes successfully,
   *     with false if failed or cancelled.
   */
  void commit(InsertCallback callback) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
};

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
