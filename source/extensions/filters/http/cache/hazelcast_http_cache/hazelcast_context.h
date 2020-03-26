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
  HazelcastLookupContextBase(HazelcastHttpCache& cache, LookupRequest&& request)
      : hz_cache_(cache), lookup_request_(std::move(request)) {
    createVariantKey(lookup_request_.key());
    variant_hash_key_ = stableHashKey(lookup_request_.key());
  }

  void getTrailers(LookupTrailersCallback&&) override {
    // TODO(enozcan): Support trailers when implemented on the filter side.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  const LookupRequest& request() const { return lookup_request_; }

  const Key& variantKey() const { return lookup_request_.key(); }

  uint64_t variantHashKey() const { return variant_hash_key_; }

  bool isAborted() const { return abort_insertion_; }

protected:

  HazelcastHttpCache& hz_cache_;
  LookupRequest lookup_request_;

  /** Hash key aware of vary headers. Lookup will be performed using this. */
  uint64_t variant_hash_key_;

  /** Flag to notice insert context created with this lookup. */
  bool abort_insertion_ = false;

private:

  /**
   * The keys created by the cache filter for lookups and inserts are not aware
   * of the vary headers of the request. Instead, cache filter expects a
   * cache to differentiate responses having the same key by their vary
   * headers. Rather than storing multiple responses with the same key and
   * then querying them according to vary headers, a different key for each
   * response including vary headers in custom fields is created here. Hence
   * responses can be found by their <variant_hash> directly.
   *
   * @param raw_key     Key created by the filter.
   */
  // TODO(enozcan): Ensure uniqueness of hash key for the same same response.
  //  Different hash keys will be created if the order of values differ for the same
  //  vary header key. Hence the response will not be affected but the same response
  //  will be cached with different keys. (might be reordering/sorting here).
  //  Prevent str copies as possible.
  void createVariantKey(Key& raw_key) {
    ASSERT(raw_key.custom_fields_size() == 0);
    ASSERT(raw_key.custom_ints_size() == 0); // Key must be pure.
    if (lookup_request_.vary_headers().size() == 0) {
      return;
    }
    std::vector<std::pair<std::string, std::string>> header_strings;

    for (const Http::HeaderEntry& header : lookup_request_.vary_headers()) {
      header_strings.push_back(std::make_pair(std::string(header.key().getStringView()),
          std::string(header.value().getStringView())));
    }
    std::sort(header_strings.begin(), header_strings.end(), [](auto& left, auto& right) -> bool {
      return left.first == right.first ? left.second < right.second : left.first < right.first;
    });
    for (auto& header : header_strings) {
      raw_key.add_custom_fields(header.first + ":" + header.second);
    }
  }

};

/**
 * Base insert context for both UNIFIED and DIVIDED cache insertions.
 */
class HazelcastInsertContextBase : public InsertContext,
                                   public Logger::Loggable<Logger::Id::hazelcast_http_cache> {
public:
  HazelcastInsertContextBase(LookupContext& lookup_context, HazelcastHttpCache& cache)
      : hz_cache_(cache), max_body_size_(cache.maxBodySize()),
      variant_hash_key_(static_cast<HazelcastLookupContextBase&>(lookup_context).variantHashKey()),
      variant_key_(static_cast<HazelcastLookupContextBase&>(lookup_context).variantKey()),
      abort_insertion_(static_cast<HazelcastLookupContextBase&>(lookup_context).isAborted()) {}

  void insertTrailers(const Http::ResponseTrailerMap&) override {
    // TODO(enozcan): Support trailers
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

protected:
  HazelcastHttpCache& hz_cache_;
  const uint64_t max_body_size_;
  bool committed_end_stream_ = false;

  // From lookup context
  const uint64_t variant_hash_key_;
  Key variant_key_;
  const bool abort_insertion_;

  // Response fields
  /** Body content is first copied into this buffer and then written to distributed map. */
  std::vector<hazelcast::byte> buffer_vector_;

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
  void flushEntry();
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
  uint64_t total_body_size_;

  int32_t version_;

  /** Max body size per body entry defined via config. */
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
  void copyIntoLocalBuffer(uint64_t& index, uint64_t& size, const Buffer::Instance& source);
  void flushBuffer();
  void flushHeader();

  /** Creates a common version for a header and its body entries.
   * This version entity secures the relation between a header and
   * its bodies hence they are stored in different distributed maps
   * in DIVIDED mode. */
  int32_t createVersion(){
    // TODO: Use a secure or Envoy style random.
    //  The <version> was designed to be the timestamp fetched from the Hazelcast
    //  cluster during insertion (clusterTime). However, it's currently not
    //  supported by the cpp client. Might be available in 4.0 release.
    //  Related issue is: https://github.com/hazelcast/hazelcast-cpp-client/issues/581
    return std::rand();
  }

  /** Counter for the order of next body entry to be inserted. */
  int body_order_ = 0;

  /** Max body size per body entry defined via config. */
  const uint64_t body_partition_size_;

  const int32_t version_;
  uint64_t total_body_size_ = 0;

};

} // HazelcastHttpCache
} // Cache
} // HttpFilters
} // Extensions
} // Envoy
