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

/**
 * Because the cache filter implementation doesn't use the "ready" callback from
 * insertHeaders or insertBody, it's possible for a body chunk to come in before
 * the cache implementation is ready to insert body chunks.
 * This struct allows us to queue up body chunks and trailers to be pushed on
 * completion of the header insert chain of actions, or prior body chunks.
 *
 * If the cache filter implementation later is changed to only deliver actions
 * when the cache implementation is ready to receive them, this could be simplified
 * significantly.
 */
struct QueuedFileChunk {
  enum class Type { Body, Trailer };
  QueuedFileChunk(Type t, const Buffer::Instance& b, InsertCallback c, bool end)
      : type(t), done_callback(c), end_stream(end) {
    // We must copy the buffer, not move it, because the original instance is being
    // consumed by streaming to the client.
    chunk.add(b);
  }
  Type type;
  Buffer::OwnedImpl chunk;
  InsertCallback done_callback;
  bool end_stream;
};

/**
 * The cache filter doesn't actually support async insertion; splitting off a
 * shared_ptr<InsertOperationQueue> to control the actual write allows the
 * queue-chain to keep itself alive until the write is completed, even after the
 * filter instance is destroyed.
 */
class InsertOperationQueue : public Logger::Loggable<Logger::Id::cache_filter> {
public:
  InsertOperationQueue(std::shared_ptr<FileSystemHttpCache>&& cache, const Key& key,
                       const Http::RequestHeaderMap& request_headers,
                       const VaryAllowList& vary_allow_list)
      : key_(key), cache_(std::move(cache)), request_headers_(request_headers),
        vary_allow_list_(vary_allow_list) {}
  /**
   * insertHeaders starts opening a cache file for write, and writing the headers
   * to it. After that write completes, if there are more write operations in the
   * queue, the next one begins. insert_complete is called when the header write
   * is complete, fails, or is cancelled.
   * If end_stream is true and the write completes, commit is also called.
   * @param p a shared_ptr to 'this', so it can be captured in lambdas to ensure
   *     'this' still exists when the lambda is called.
   * @param response_headers the http headers to be written to the cache file.
   * @param metadata the metadata to be written to the cache file.
   * @param insert_complete is called with true when the header write completes,
   *     or with false if file-open or any of the writes fail.
   * @param end_stream true if the cache entry is header-only.
   */
  void insertHeaders(std::shared_ptr<InsertOperationQueue> p,
                     const Http::ResponseHeaderMap& response_headers,
                     const ResponseMetadata& metadata, InsertCallback insert_complete,
                     bool end_stream);
  /**
   * insertBody queues writing a body chunk to the cache file previously opened
   * in insertHeaders. If there were previously no operations in the queue, that
   * write operation begins. After that write completes, if there are more operations
   * in the queue, the next operation begins. ready_for_next_chunk is called
   * when the write completes, fails, or is cancelled.
   * If end_stream is true and the write completes, commit is also called.
   * @param p a shared_ptr to 'this', so it can be captured in lambdas to ensure
   *     'this' still exists when the lambda is called.
   * @param chunk a buffer of body data to be written to the cache file.
   * @param ready_for_next_chunk is called with true when the chunk write completes,
   *     or with false if the write fails.
   * @param end_stream true if this is the last body chunk and there are no trailers.
   */
  void insertBody(std::shared_ptr<InsertOperationQueue> p, const Buffer::Instance& chunk,
                  InsertCallback ready_for_next_chunk, bool end_stream);
  /**
   * insertTrailers queues writing the trailers to the cache file previously opened
   * in insertHeaders. If there were previously no operations in the queue, that
   * write operation begins. After that write completes, commit is called.
   * insert_complete is called when the write completes, fails, or is cancelled.
   * @param p a shared_ptr to 'this', so it can be captured in lambdas to ensure
   *     'this' still exists when the lambda is called.
   * @param response_trailers the trailers to be written to the cache file.
   * @param insert_complete is called with true when the commit completes,
   *     or with false if any part of the commit fails or if it was not called.
   */
  void insertTrailers(std::shared_ptr<InsertOperationQueue> p,
                      const Http::ResponseTrailerMap& response_trailers,
                      InsertCallback insert_complete);

  /**
   * If seen_end_stream_ is not true (i.e. InsertContext has not yet delivered the
   * entire response), cancel insertion. Called by InsertContext onDestroy.
   */
  void cancelIfIncomplete(std::shared_ptr<InsertOperationQueue> p);

  /**
   * Takes the mutex and calls cancelInsert.
   */
  ~InsertOperationQueue();

private:
  /**
   * Puts an operation into the queue, or, if the queue is empty and no operation
   * is in flight, starts the operation immediately.
   * @param p a shared_ptr to 'this', so it can be captured in lambdas to ensure
   *     'this' still exists when the lambda is called.
   * @param chunk the data to be written.
   */
  void push(std::shared_ptr<InsertOperationQueue> p, QueuedFileChunk&& chunk)
      ABSL_LOCKS_EXCLUDED(mu_);

  /**
   * Cancels any action in flight, calls any uncalled completion callbacks with false,
   * and closes the file if open.
   * @param p a shared_ptr to 'this'. Not actually used during cancelInsert, but taken
   *     as a parameter to enforce that the caller must have the shared_ptr. May be
   *     nullptr if called from the destructor.
   * @param err a string to log with the failure.
   */
  void cancelInsert(std::shared_ptr<InsertOperationQueue> p, absl::string_view err = "")
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

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
  void commit(std::shared_ptr<InsertOperationQueue> p, InsertCallback callback)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  /**
   * Starts async-writing a chunk of data.
   * Calls the chunk's callback on completion, failure or cancellation.
   * @param p a shared_ptr to 'this', so it can be captured in lambdas to ensure
   *     'this' still exists when the lambda is called.
   * @param chunk contains the data to be written, and the metadata about where
   *     to write it.
   */
  void writeChunk(std::shared_ptr<InsertOperationQueue> p, QueuedFileChunk&& chunk)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  /**
   * Starts async-writing a previously queued chunk of data.
   * Calls the chunk's callback on completion, failure or cancellation.
   * If the queue is empty, does nothing.
   * @param p a shared_ptr to 'this', so it can be captured in lambdas to ensure
   *     'this' still exists when the lambda is called.
   */
  void writeNextChunk(std::shared_ptr<InsertOperationQueue> p) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  absl::Mutex mu_;
  std::shared_ptr<Cleanup> cleanup_ ABSL_GUARDED_BY(mu_);
  std::deque<QueuedFileChunk> queue_ ABSL_GUARDED_BY(mu_);
  AsyncFileHandle file_handle_ ABSL_GUARDED_BY(mu_);
  CancelFunction cancel_action_in_flight_ ABSL_GUARDED_BY(mu_);
  std::function<void(bool)> callback_in_flight_ ABSL_GUARDED_BY(mu_);
  CacheFileFixedBlock header_block_ ABSL_GUARDED_BY(mu_);
  CacheFileHeader header_proto_ ABSL_GUARDED_BY(mu_);
  bool seen_end_stream_ ABSL_GUARDED_BY(mu_) = false;
  // key_ may be updated to a varyKey during insertHeaders.
  Key key_ ABSL_GUARDED_BY(mu_);
  const std::shared_ptr<FileSystemHttpCache> cache_;
  // Request-related values. These are only used during the header callback, so they
  // can be a reference as they exist in the filter which cannot be destroyed before that.
  const Http::RequestHeaderMap& request_headers_;
  const VaryAllowList& vary_allow_list_;
};

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

class FileInsertContext : public InsertContext {
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
  std::shared_ptr<InsertOperationQueue> queue_;
};

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
