#pragma once

#include <memory>

#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/filters/http/cache/file_system_http_cache/cache_entry.h"
#include "source/extensions/filters/http/cache/file_system_http_cache/cache_file_header.pb.h"
#include "source/extensions/filters/http/cache/http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

using ::Envoy::Extensions::Common::AsyncFiles::AsyncFileHandle;
using ::Envoy::Extensions::Common::AsyncFiles::CancelFunction;

class FileLookupContext;
class FileSystemHttpCache;

// Because the cache implementation doesn't use a "ready" callback from insertHeaders,
// it's possible for a body chunk to come in before we're ready to insert body chunks.
// This struct allows us to queue up that first body chunk to be pushed on completion
// of the header insert chain.
// And then the implementation doesn't actually *use* the callback from insertBody, or
// wait for it to be called, so we have to actually just support queuing any amount of
// pieces.
struct QueuedFileChunk {
  enum class Type { Body, Trailer };
  QueuedFileChunk(Type t, const Buffer::Instance& b, InsertCallback c, bool end)
      : type(t), done_callback(c), end_stream(end) {
    chunk.add(b);
  }
  Type type;
  Buffer::OwnedImpl chunk;
  InsertCallback done_callback;
  bool end_stream;
};

// The cache filter doesn't actually support async insertion; splitting off a
// shared_ptr to control the actual write allows the queue-chain to keep itself alive
// until the write is completed.
class InsertOperationQueue : public Logger::Loggable<Logger::Id::cache_filter> {
public:
  InsertOperationQueue(std::shared_ptr<FileSystemHttpCache>&& cache, const Key& key,
                       const Http::RequestHeaderMap& request_headers,
                       const VaryAllowList& vary_allow_list)
      : key_(key), cache_(std::move(cache)), request_headers_(request_headers),
        vary_allow_list_(vary_allow_list) {}
  void insertHeaders(std::shared_ptr<InsertOperationQueue> p,
                     const Http::ResponseHeaderMap& response_headers,
                     const ResponseMetadata& metadata, InsertCallback insert_complete,
                     bool end_stream);
  void insertBody(std::shared_ptr<InsertOperationQueue> p, const Buffer::Instance& chunk,
                  InsertCallback ready_for_next_chunk, bool end_stream);
  void insertTrailers(std::shared_ptr<InsertOperationQueue> p,
                      const Http::ResponseTrailerMap& response_trailers,
                      InsertCallback insert_complete);
  // Destruction cancels any insert in progress and removes the temporary cache entry.
  // If destruction is after insert is completed, it does nothing.
  ~InsertOperationQueue();

private:
  absl::Mutex mu_;
  void push(std::shared_ptr<InsertOperationQueue> p, QueuedFileChunk&& chunk)
      ABSL_LOCKS_EXCLUDED(mu_);
  // cancelInsert doesn't use the shared_ptr, but taking it as a parameter ensures we don't
  // accidentally call it from somewhere where we haven't captured the shared_ptr. The only
  // place the shared_ptr should be null is in the destructor.
  void cancelInsert(std::shared_ptr<InsertOperationQueue> p, absl::string_view err = "")
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void commit(std::shared_ptr<InsertOperationQueue> p, InsertCallback callback)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void writeChunk(std::shared_ptr<InsertOperationQueue> p, QueuedFileChunk&& chunk)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void writeNextChunk(std::shared_ptr<InsertOperationQueue> p) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  std::shared_ptr<CacheEntryFile> cache_entry_ ABSL_GUARDED_BY(mu_);
  std::deque<QueuedFileChunk> queue_ ABSL_GUARDED_BY(mu_);
  AsyncFileHandle file_handle_ ABSL_GUARDED_BY(mu_);
  CancelFunction cancel_action_in_flight_ ABSL_GUARDED_BY(mu_);
  CacheFileFixedBlock header_block_ ABSL_GUARDED_BY(mu_);
  off_t write_pos_ ABSL_GUARDED_BY(mu_);
  // key_ may be updated to a varyKey during insertHeaders.
  Key key_ ABSL_GUARDED_BY(mu_);
  const std::shared_ptr<FileSystemHttpCache> cache_;
  // Request-related values. These are only used during the header callback, so they
  // can be references as they exist in the filter which cannot be destroyed before that.
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
  FileInsertContext(std::shared_ptr<FileSystemHttpCache> cache, const Key& key,
                    const Http::RequestHeaderMap& request_headers,
                    const VaryAllowList& vary_allow_list);
  void insertHeaders(const Http::ResponseHeaderMap& response_headers,
                     const ResponseMetadata& metadata, InsertCallback insert_complete,
                     bool end_stream) override;
  void insertBody(const Buffer::Instance& chunk, InsertCallback ready_for_next_chunk,
                  bool end_stream) override;
  void insertTrailers(const Http::ResponseTrailerMap& trailers,
                      InsertCallback insert_complete) override;
  void onDestroy() override {} // Destruction of queue_ is all the cleanup.

private:
  std::shared_ptr<InsertOperationQueue> queue_;
};

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
