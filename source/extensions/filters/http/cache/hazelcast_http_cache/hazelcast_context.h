#pragma once

#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

/**
 * Base lookup context for both UNIFIED and DIVIDED cache lookups.
 */
class HazelcastLookupContextBase : public LookupContext,
                                   public Logger::Loggable<Logger::Id::hazelcast_http_cache> {
public:
  HazelcastLookupContextBase(HazelcastHttpCache& cache, LookupRequest&& request);

  // LookupContext
  void getTrailers(LookupTrailersCallback&&) override;

  const Key& variantKey() const { return lookup_request_.key(); }
  uint64_t variantKeyHash() const { return variant_key_hash_; }
  bool isAborted() const { return abort_insertion_; }

protected:
  void handleLookupFailure(absl::string_view message, const LookupHeadersCallback& cb,
                           bool warn_log = true);

  HazelcastHttpCache& hz_cache_;
  LookupRequest lookup_request_;

  /** Hash key aware of vary headers. Lookup to header and response entry is performed using this
   * key. */
  uint64_t variant_key_hash_;

  /** Flag to notice insert context created for this lookup */
  bool abort_insertion_ = false;

private:
  friend class HazelcastHttpCacheTest;

  /**
   * The keys created by the cache filter for lookups and inserts are not aware
   * of the vary headers of the request. Instead, cache filter expects a
   * cache plugin to differentiate responses having the same key by their vary
   * headers. Rather than storing multiple responses with the same key and
   * then querying them according to vary headers, a different key for each
   * response including vary headers in custom fields is created here. Hence
   * responses can be found by their <variant_key_hash> directly without querying.
   *
   * @param raw_key     Key to be modified created by the filter.
   */
  void createVariantKey(Key& raw_key);

  /**
   * Fills the custom_fields of a key with given headers in alphabetical order.
   *
   * @note  Decoupled from createVariantKey for testing.
   * @param raw_key     Key to be modified created by the filter.
   * @param headers     Vary headers to be included in the key.
   */
  void arrangeVariantHeaders(Key& raw_key,
                             std::vector<std::pair<std::string, std::string>>& headers);
};

/**
 * Base insert context for both UNIFIED and DIVIDED cache insertions.
 */
class HazelcastInsertContextBase : public InsertContext,
                                   public Logger::Loggable<Logger::Id::hazelcast_http_cache> {
public:
  HazelcastInsertContextBase(LookupContext& lookup_context, HazelcastHttpCache& cache);

  // InsertContext
  void insertTrailers(const Http::ResponseTrailerMap&) override;

protected:
  HazelcastHttpCache& hz_cache_;

  // From HazelcastHttpCache configuration
  const uint64_t max_body_size_;

  bool committed_end_stream_ = false;

  // Derived from lookup context
  const uint64_t variant_key_hash_;
  Key variant_key_;
  const bool abort_insertion_;

  // Response fields
  /** Body content is first copied into this buffer and then written to distributed map. */
  std::vector<hazelcast::byte> buffer_vector_;

  /** Response headers to be inserted */
  Http::ResponseHeaderMapPtr header_map_;
};

/**
 * Lookup context for UNIFIED cache.
 */
class UnifiedLookupContext : public HazelcastLookupContextBase {
public:
  UnifiedLookupContext(HazelcastHttpCache& cache, LookupRequest&& request);
  void getHeaders(LookupHeadersCallback&& cb) override;
  void getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb) override;

private:
  /** Response to be inserted */
  HazelcastResponsePtr response_;
};

/**
 * Insert context for UNIFIED cache.
 */
class UnifiedInsertContext : public HazelcastInsertContextBase {
public:
  UnifiedInsertContext(LookupContext& lookup_context, HazelcastHttpCache& cache);
  void insertHeaders(const Http::ResponseHeaderMap& response_headers, bool end_stream) override;
  void insertBody(const Buffer::Instance& chunk, InsertCallback ready_for_next_chunk,
                  bool end_stream) override;

private:
  /**
   * Wraps the current response content with HazelcastResponseEntry and puts
   * into the cache.
   */
  void insertResponse();
};

/**
 * Lookup context for DIVIDED cache.
 */
class DividedLookupContext : public HazelcastLookupContextBase {
public:
  DividedLookupContext(HazelcastHttpCache& cache, LookupRequest&& request);
  void getHeaders(LookupHeadersCallback&& cb) override;
  void getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb) override;

private:
  void handleBodyLookupFailure(absl::string_view message, const LookupBodyCallback& cb,
                               bool warn_log = true);

  /** Values fetched from the cache after a successful lookup */
  bool found_header_ = false;
  int32_t version_;
  uint64_t total_body_size_;

  /** Max body size per body entry defined via cache config. */
  const uint64_t body_partition_size_;
};

/**
 * Insert context for DIVIDED cache.
 */
class DividedInsertContext : public HazelcastInsertContextBase {
public:
  DividedInsertContext(LookupContext& lookup_context, HazelcastHttpCache& cache);
  void insertHeaders(const Http::ResponseHeaderMap& response_headers, bool end_stream) override;
  void insertBody(const Buffer::Instance& chunk, InsertCallback ready_for_next_chunk,
                  bool end_stream) override;

private:
  /**
   * Copies bytes from source to local buffer. Insertion to the cache happens after
   * the local buffer is full or the end stream is committed by the filter.
   * @param offset      Byte offset for the source. Updated <size> much after copying.
   * @param size        Number of bytes to be copied into the local buffer.
   * @param source      Body content given by the filter.
   */
  void copyIntoLocalBuffer(uint64_t& offset, uint64_t size, const Buffer::Instance& source);

  /**
   * Wraps the current body buffer with HazelcastBodyEntry and puts
   * into the cache.
   *
   * @return True if insertion is completed.
   */
  bool flushBuffer();

  /**
   * Wraps the current header map, request key, body size and version values with
   * HazelcastHeaderEntry and puts into the cache.
   */
  void insertHeader();

  /**
   * Creates a common version for a header and its body entries.
   * This version denotes the relation between a header and its
   * bodies such that they are inserted by the same insert context
   * for the same lookup in DIVIDED mode.
   */
  int32_t createVersion();

  /** Counter for the order of next body entry to be inserted. */
  int body_order_ = 0;

  /** Max body size per body entry defined via config. */
  const uint64_t body_partition_size_;

  /** Response specific values to be used in the cached entries */
  const int32_t version_;
  uint64_t total_body_size_ = 0;

  bool insertion_allowed_ = true;
};

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
