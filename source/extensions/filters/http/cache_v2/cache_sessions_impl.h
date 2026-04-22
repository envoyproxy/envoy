#pragma once

#include "envoy/buffer/buffer.h"

#include "source/common/common/cancel_wrapper.h"
#include "source/extensions/filters/http/cache_v2/cache_sessions.h"
#include "source/extensions/filters/http/cache_v2/upstream_request.h"

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "stats.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

class CacheSession;
class CacheSessionsImpl;

class ActiveLookupContext : public HttpSource {
public:
  ActiveLookupContext(ActiveLookupRequestPtr lookup, std::shared_ptr<CacheSession> entry,
                      uint64_t content_length = 0)
      : lookup_(std::move(lookup)), entry_(entry), content_length_(content_length) {}
  // HttpSource
  void getHeaders(GetHeadersCallback&& cb) override;
  void getBody(AdjustedByteRange range, GetBodyCallback&& cb) override;
  void getTrailers(GetTrailersCallback&& cb) override;

  Event::Dispatcher& dispatcher() const { return lookup().dispatcher(); }
  ActiveLookupRequest& lookup() const { return *lookup_; }

  void setContentLength(uint64_t l) { content_length_ = l; }

private:
  ActiveLookupRequestPtr lookup_;
  std::shared_ptr<CacheSession> entry_;
  uint64_t content_length_;
};

class CacheSession : public Logger::Loggable<Logger::Id::cache_filter>,
                     public CacheProgressReceiver,
                     public std::enable_shared_from_this<CacheSession> {
public:
  CacheSession(std::weak_ptr<CacheSessionsImpl> cache_sessions, const Key& key);

  // CacheProgressReceiver
  void onHeadersInserted(CacheReaderPtr cache_reader, Http::ResponseHeaderMapPtr headers,
                         bool end_stream) override;
  void onBodyInserted(AdjustedByteRange range, bool end_stream) override;
  void onTrailersInserted(Http::ResponseTrailerMapPtr trailers) override;
  void onInsertFailed(absl::Status status) override;

  void getLookupResult(ActiveLookupRequestPtr lookup,
                       ActiveLookupResultCallback&& lookup_result_callback)
      ABSL_LOCKS_EXCLUDED(mu_);
  void onCacheLookupResult(absl::StatusOr<LookupResult>&& result) ABSL_LOCKS_EXCLUDED(mu_);

  void wantHeaders(Event::Dispatcher& dispatcher, SystemTime lookup_timestamp,
                   GetHeadersCallback&& cb) ABSL_LOCKS_EXCLUDED(mu_);
  void wantBodyRange(AdjustedByteRange range, Event::Dispatcher& dispatcher, GetBodyCallback&& cb)
      ABSL_LOCKS_EXCLUDED(mu_);
  void wantTrailers(Event::Dispatcher& dispatcher, GetTrailersCallback&& cb)
      ABSL_LOCKS_EXCLUDED(mu_);
  void clearUncacheableState() ABSL_LOCKS_EXCLUDED(mu_);

  ~CacheSession();

  class Subscriber {
  public:
    explicit Subscriber(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}
    Event::Dispatcher& dispatcher() { return dispatcher_.get(); }

  private:
    // In order to be moveable in a vector we can't use a plain reference.
    std::reference_wrapper<Event::Dispatcher> dispatcher_;
  };
  class BodySubscriber : public Subscriber {
  public:
    BodySubscriber(Event::Dispatcher& dispatcher, AdjustedByteRange range, GetBodyCallback&& cb)
        : Subscriber(dispatcher), callback_(std::move(cb)), range_(std::move(range)) {}
    GetBodyCallback callback_;
    AdjustedByteRange range_;
  };
  class TrailerSubscriber : public Subscriber {
  public:
    TrailerSubscriber(Event::Dispatcher& dispatcher, GetTrailersCallback&& cb)
        : Subscriber(dispatcher), callback_(std::move(cb)) {}
    GetTrailersCallback callback_;
  };
  class LookupSubscriber : public Subscriber {
  public:
    LookupSubscriber(std::unique_ptr<ActiveLookupContext> context, ActiveLookupResultCallback&& cb)
        : Subscriber(context->dispatcher()), callback_(std::move(cb)),
          context_(std::move(context)) {}
    ActiveLookupResultCallback callback_;
    std::unique_ptr<ActiveLookupContext> context_;
  };

private:
  enum class State {
    // New state means this is the first client of the cache entry - it should immediately
    // update the state to Pending and attempt a lookup (then if necessary insertion).
    New,
    // Pending state means another client is already doing lookup/insertion/verification.
    // Client should subscribe to this, and act on received messages.
    Pending,
    // Inserting state means a cache entry exists but has not yet completed writing.
    Inserting,
    // Exists state means a cache entry probably exists. Client should attempt to read from
    // the entry. On cache failure, state should revert to New. On expiry, state should become
    // Validating.
    Exists,
    // Validating state means the cache entry exists but either is expired or some header has
    // explicitly required validation from upstream.
    Validating,
    // Vary state means the cache entry includes headers and the request must be
    // re-keyed onto the appropriate variation key.
    Vary,
    // NotCacheable state means this key is considered non-cacheable. Client should pass through.
    // If the passed-through response turns out to be cacheable (i.e. upstream has changed
    // cache headers), client should update state to Writing, or, if state is already changed,
    // client should abort the new upstream request and use the shared one.
    NotCacheable
  };

  EndStream endStreamAfterHeaders() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  EndStream endStreamAfterBody() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Switches state to Written, removes the insert_context_, notifies all
  // subscribers.
  void insertComplete() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Switches state to New, removes the insert_context_, resets all subscribers.
  // Ideally this shouldn't happen, but an unreliable upstream could cause it.
  // TODO(ravenblackx): this could theoretically be improved with a retry process
  // rather than resetting all the downstreams on error, but that's beyond MVP.
  void insertAbort() ABSL_LOCKS_EXCLUDED(mu_);

  void headersWritten(const Http::ResponseHeaderMap&& response_headers,
                      ResponseMetadata&& response_metadata,
                      absl::optional<uint64_t> content_length_override, bool end_stream)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Populates the headers in memory.
  void saveHeaders(const Http::ResponseHeaderMap&& response_headers,
                   ResponseMetadata&& response_metadata, absl::optional<uint64_t> content_length,
                   bool end_stream) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  bool requiresValidationFor(const ActiveLookupRequest& lookup) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // For each subscriber, either sends a lookup response (if validation passes), or
  // triggers validation *once* for all subscribers for whom validation failed.
  // If an insert occurred then first_status should be Miss, otherwise Hit.
  void sendLookupResponsesAndMaybeValidationRequest(
      CacheEntryStatus first_status = CacheEntryStatus::Hit) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Sends an upstream validation request.
  void performValidation() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void processSuccessfulValidation(Http::ResponseHeaderMapPtr headers)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // If the headers include vary, update all blocked subscribers with their new keys
  // and returns true. Otherwise returns false.
  bool handleVary(const Http::ResponseHeaderMap&& response_headers)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Called by the InsertContext.
  // Updates the state to reflect the increased availability, and
  // triggers a file-read action if there is a subscriber waiting on a body chunk
  // within the available range, and no read file action is in flight.
  void bodyWrittenTo(uint64_t sz, bool end_stream) ABSL_LOCKS_EXCLUDED(mu_);

  // Called by the InsertContext.
  // Populates the trailers in memory, and calls sendTrailers.
  void trailersWritten(Http::ResponseTrailerMapPtr response_trailers) ABSL_LOCKS_EXCLUDED(mu_);

  // Attempts to open the cache file.
  //
  // On failure notifies the first queued LookupContext of a cache miss, so
  // the cache entry can be either populated or marked as uncacheable.
  //
  // On success, attempts to validate the cache entry.
  //
  // If it is valid, all queued LookupContexts are notified to use the file.
  //
  // If it is not valid, attempts to populate the cache entry.
  //
  // If attempt to populate the cache entry fails, marks as uncacheable,
  // hands the UpstreamRequest to the first LookupContext, and notifies the
  // rest of the queue that the result is uncacheable and they should bypass
  // the cache, or, if the original request had a range header which was
  // discarded for the UpstreamRequest, the UpstreamRequest is reset and *all*
  // LookupContexts are notified to bypass the cache.
  void sendSuccessfulLookupResultTo(LookupSubscriber& subscriber, CacheEntryStatus status)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void checkCacheEntryExistence(Event::Dispatcher& dispatcher) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void onCacheEntryExistence(LookupResult&& lookup_result) ABSL_LOCKS_EXCLUDED(mu_);
  void sendBodyChunkTo(BodySubscriber& subscriber, AdjustedByteRange range, Buffer::InstancePtr buf)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void sendTrailersTo(TrailerSubscriber& subscriber) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void sendAbortTo(Subscriber& subscriber) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  bool tryEnqueueBodyChunk(BodySubscriber& subscriber) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  // If there's not already a read operation in flight and any requested
  // range is within the available range, start an operation to
  // read that range (prioritized by oldest subscriber).
  void maybeTriggerBodyReadForWaitingSubscriber() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  bool selectBodyToRead() ABSL_LOCKS_EXCLUDED(mu_);
  void abortBodyOutOfRangeSubscribers() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  bool canReadBodyRangeFromCacheEntry(BodySubscriber& subscriber);
  void onBodyChunkFromCache(AdjustedByteRange range, Buffer::InstancePtr buffer,
                            EndStream end_stream) ABSL_LOCKS_EXCLUDED(mu_);
  void onCacheError() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  void doCacheEntryInvalid() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void doCacheMiss() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void validateCacheEntry(Event::Dispatcher& dispatcher) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void performUpstreamRequest() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void onUpstreamHeaders(Http::ResponseHeaderMapPtr headers, EndStream end_stream,
                         bool range_header_was_stripped) ABSL_LOCKS_EXCLUDED(mu_);
  void onUncacheable(Http::ResponseHeaderMapPtr headers, EndStream end_stream,
                     bool range_header_was_stripped) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  // For the unlikely case that cache config was modified while operations were in flight,
  // requests still in the lookup state are transformed to pass-through.
  // Requests for headers/body/trailers should be able to continue as the cache
  // *entries* can outlive the cache object itself as long as they're in use.
  void onCacheWentAway() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // May change state from New to Pending, or from Written to Validating.
  // When changing state, also makes the corresponding upstream request.
  void mutateStateForHeaderRequest(const LookupRequest& lookup) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  bool headersAreReady() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  mutable absl::Mutex mu_;
  State state_ ABSL_GUARDED_BY(mu_) = State::New;
  uint64_t content_length_header_ = 0;
  LookupResult entry_ ABSL_GUARDED_BY(mu_);
  // While streaming this is a proxy for body_length_ which should not
  // be populated in entry_ until the insert is complete.
  uint64_t body_length_available_ = 0;
  std::weak_ptr<CacheSessionsImpl> cache_sessions_;
  Key key_;
  bool in_body_loop_callback_ = false;

  std::vector<LookupSubscriber> lookup_subscribers_ ABSL_GUARDED_BY(mu_);
  std::vector<BodySubscriber> body_subscribers_ ABSL_GUARDED_BY(mu_);
  std::vector<TrailerSubscriber> trailer_subscribers_ ABSL_GUARDED_BY(mu_);
  UpstreamRequestPtr upstream_request_ ABSL_GUARDED_BY(mu_);
  bool read_action_in_flight_ ABSL_GUARDED_BY(mu_) = false;

  // The following fields and functions are only used by CacheSessions.
  friend class CacheSessionsImpl;
  bool inserting() const {
    absl::MutexLock lock(mu_);
    return state_ == State::Inserting;
  }
  void setExpiry(SystemTime expiry) { expires_at_ = expiry; }
  bool isExpiredAt(SystemTime t) const { return expires_at_ < t && !inserting(); }

  SystemTime expires_at_; // This is guarded by CacheSessions's mutex.

  // An arbitrary 256k limit on per-read fragment size.
  // TODO(ravenblack): Make this configurable?
  static constexpr uint64_t max_read_chunk_size_ = 256 * 1024;
};

class CacheSessionsImpl : public CacheSessions,
                          public std::enable_shared_from_this<CacheSessionsImpl> {
public:
  CacheSessionsImpl(Server::Configuration::FactoryContext& context,
                    std::unique_ptr<HttpCache> cache)
      : time_source_(context.serverFactoryContext().timeSource()), cache_(std::move(cache)),
        stats_(generateStats(context.scope(), cache_->cacheInfo().name_)) {}

  void lookup(ActiveLookupRequestPtr request, ActiveLookupResultCallback&& cb) override;
  CacheFilterStats& stats() const override { return *stats_; }

  ResponseMetadata makeMetadata();

  HttpCache& cache() const override { return *cache_; }

private:
  // Returns an entry with the given key, creating it if necessary.
  std::shared_ptr<CacheSession> getEntry(const Key& key) ABSL_LOCKS_EXCLUDED(mu_);

  TimeSource& time_source_;
  std::unique_ptr<HttpCache> cache_;
  CacheFilterStatsPtr stats_;
  std::chrono::duration<int> expiry_duration_ = std::chrono::minutes(5);
  mutable absl::Mutex mu_;
  // If there turns out to be problematic contention on this mutex, this could
  // easily be turned into a simple short-hash-keyed array of maps each with
  // their own mutex. Since it's only held for a short time and is related to
  // async operations, it seems unlikely that mutex contention would be a
  // significant bottleneck.
  absl::flat_hash_map<Key, std::shared_ptr<CacheSession>, MessageUtil, MessageUtil>
      entries_ ABSL_GUARDED_BY(mu_);

  friend class CacheSession;
};

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
