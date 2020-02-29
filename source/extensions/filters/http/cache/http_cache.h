#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/time.h"
#include "envoy/config/typed_config.h"
#include "envoy/extensions/filters/http/cache/v3alpha/cache.pb.h"
#include "envoy/http/header_map.h"

#include "common/common/assert.h"

#include "source/extensions/filters/http/cache/key.pb.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
// Whether a given cache entry is good for the current request.
enum class CacheEntryStatus {
  // This entry is fresh, and an appropriate response to the request.
  Ok,
  // No usable entry was found. If this was generated for a cache entry, the
  // cache should delete that entry.
  Unusable,
  // This entry is stale, but appropriate for validating
  RequiresValidation,
  // This entry is fresh, and an appropriate basis for a 304 Not Modified
  // response.
  FoundNotModified,
  // This entry is fresh, but can't satisfy the requested range(s).
  UnsatisfiableRange,
};

std::ostream& operator<<(std::ostream& os, CacheEntryStatus status);

// Byte range from an HTTP request.
class RawByteRange {
public:
  // - If first==UINT64_MAX, construct a RawByteRange requesting the final last body bytes.
  // - Otherwise, construct a RawByteRange requesting the [first,last] body bytes.
  // Prereq: first == UINT64_MAX || first <= last
  // Invariant: isSuffix() || firstBytePos() <= lastBytePos
  // Examples: RawByteRange(0,4) requests the first 5 bytes.
  //           RawByteRange(UINT64_MAX,4) requests the last 4 bytes.
  RawByteRange(uint64_t first, uint64_t last) : first_byte_pos_(first), last_byte_pos_(last) {
    ASSERT(isSuffix() || first <= last, "Illegal byte range.");
  }
  bool isSuffix() const { return first_byte_pos_ == UINT64_MAX; }
  uint64_t firstBytePos() const {
    ASSERT(!isSuffix());
    return first_byte_pos_;
  }
  uint64_t lastBytePos() const {
    ASSERT(!isSuffix());
    return last_byte_pos_;
  }
  uint64_t suffixLength() const {
    ASSERT(isSuffix());
    return last_byte_pos_;
  }

private:
  const uint64_t first_byte_pos_;
  const uint64_t last_byte_pos_;
};

// Byte range from an HTTP request, adjusted for a known response body size, and converted from an
// HTTP-style closed interval to a C++ style half-open interval.
class AdjustedByteRange {
public:
  // Construct an AdjustedByteRange representing the [first,last) bytes in the
  // response body. Prereq: first <= last Invariant: begin() <= end()
  // Example: AdjustedByteRange(0,4) represents the first 4 bytes.
  AdjustedByteRange(uint64_t first, uint64_t last) : first_(first), last_(last) {
    ASSERT(first < last, "Illegal byte range.");
  }
  uint64_t begin() const { return first_; }
  // Unlike RawByteRange, end() is one past the index of the last offset.
  uint64_t end() const { return last_; }
  uint64_t length() const { return last_ - first_; }
  void trimFront(uint64_t n) {
    ASSERT(n <= length(), "Attempt to trim too much from range.");
    first_ += n;
  }

private:
  uint64_t first_;
  uint64_t last_;
};

inline bool operator==(const AdjustedByteRange& lhs, const AdjustedByteRange& rhs) {
  return lhs.begin() == rhs.begin() && lhs.end() == rhs.end();
}

std::ostream& operator<<(std::ostream& os, const AdjustedByteRange& range);

// Adjusts request_range_spec to fit a cached response of size content_length, putting the results
// in response_ranges. Returns true if response_ranges is satisfiable (empty is considered
// satisfiable, as it denotes the entire body).
// TODO(toddmgreer): Merge/reorder ranges where appropriate.
bool adjustByteRangeSet(std::vector<AdjustedByteRange>& response_ranges,
                        const std::vector<RawByteRange>& request_range_spec,
                        uint64_t content_length);

// Result of a lookup operation, including cached headers and information needed
// to serve a response based on it, or to attempt to validate.
struct LookupResult {
  // If cache_entry_status_ == Unusable, none of the other members are
  // meaningful.
  CacheEntryStatus cache_entry_status_ = CacheEntryStatus::Unusable;

  // Headers of the cached response.
  Http::ResponseHeaderMapPtr headers_;

  // Size of the full response body. Cache filter will generate a content-length
  // header with this value, replacing any preexisting content-length header.
  // (This lets us dechunk responses as we insert them, then later serve them
  // with a content-length header.)
  uint64_t content_length_;

  // Represents the subset of the cached response body that should be served to
  // the client. If response_ranges.empty(), the entire body should be served.
  // Otherwise, each Range in response_ranges specifies an exact set of bytes to
  // serve from the cached response's body. All byte positions in
  // response_ranges must be in the range [0,content_length). Caches should
  // ensure that they can efficiently serve these ranges, and may merge and/or
  // reorder ranges as appropriate, or may clear() response_ranges entirely.
  std::vector<AdjustedByteRange> response_ranges_;

  // TODO(toddmgreer): Implement trailer support.
  // True if the cached response has trailers.
  bool has_trailers_ = false;
};

// Produces a hash of key that is consistent across restarts, architectures,
// builds, and configurations. Caches that store persistent entries based on a
// 64-bit hash should (but are not required to) use stableHashKey. Once this API
// leaves alpha, any improvements to stableHashKey that would change its output
// for existing callers is a breaking change.
//
// For non-persistent storage, use MessageUtil, which has no long-term stability
// guarantees.
//
// When providing a cached response, Caches must ensure that the keys (and not
// just their hashes) match.
//
// TODO(toddmgreer): Ensure that stability guarantees above are accurate.
size_t stableHashKey(const Key& key);

// LookupRequest holds everything about a request that's needed to look for a
// response in a cache, to evaluate whether an entry from a cache is usable, and
// to determine what ranges are needed.
class LookupRequest {
public:
  using HeaderVector = std::vector<Http::HeaderEntry>;

  // Prereq: request_headers's Path(), Scheme(), and Host() are non-null.
  LookupRequest(const Http::RequestHeaderMap& request_headers, SystemTime timestamp);

  // Caches may modify the key according to local needs, though care must be
  // taken to ensure that meaningfully distinct responses have distinct keys.
  const Key& key() const { return key_; }
  Key& key() { return key_; }

  // Returns the subset of this request's headers that are listed in
  // envoy::extensions::filters::http::cache::v3alpha::CacheConfig::allowed_vary_headers. If a cache
  // storage implementation forwards lookup requests to a remote cache server that supports *vary*
  // headers, that server may need to see these headers. For local implementations, it may be
  // simpler to instead call makeLookupResult with each potential response.
  HeaderVector& vary_headers() { return vary_headers_; }
  const HeaderVector& vary_headers() const { return vary_headers_; }

  // Time when this LookupRequest was created (in response to an HTTP request).
  SystemTime timestamp() const { return timestamp_; }

  // WARNING: Incomplete--do not use in production (yet).
  // Returns a LookupResult suitable for sending to the cache filter's
  // LookupHeadersCallback. Specifically,
  // - LookupResult::cache_entry_status_ is set according to HTTP cache
  // validation logic.
  // - LookupResult::headers takes ownership of response_headers.
  // - LookupResult::content_length == content_length.
  // - LookupResult::response_ranges entries are satisfiable (as documented
  // there).
  LookupResult makeLookupResult(Http::ResponseHeaderMapPtr&& response_headers,
                                uint64_t content_length) const;

private:
  bool isFresh(const Http::ResponseHeaderMap& response_headers) const;

  Key key_;
  std::vector<RawByteRange> request_range_spec_;
  SystemTime timestamp_;
  HeaderVector vary_headers_;
  const std::string request_cache_control_;
};

// Statically known information about a cache.
struct CacheInfo {
  absl::string_view name_;
  bool supports_range_requests_ = false;
};

using LookupBodyCallback = std::function<void(Buffer::InstancePtr&&)>;
using LookupHeadersCallback = std::function<void(LookupResult&&)>;
using LookupTrailersCallback = std::function<void(Http::ResponseTrailerMapPtr&&)>;
using InsertCallback = std::function<void(bool success_ready_for_more)>;

// Manages the lifetime of an insertion.
class InsertContext {
public:
  // Accepts response_headers for caching. Only called once.
  virtual void insertHeaders(const Http::ResponseHeaderMap& response_headers, bool end_stream) PURE;

  // The insertion is streamed into the cache in chunks whose size is determined
  // by the client, but with a pace determined by the cache. To avoid streaming
  // data into cache too fast for the cache to handle, clients should wait for
  // the cache to call ready_for_next_chunk() before streaming the next chunk.
  //
  // The client can abort the streaming insertion by dropping the
  // InsertContextPtr. A cache can abort the insertion by passing 'false' into
  // ready_for_next_chunk.
  virtual void insertBody(const Buffer::Instance& chunk, InsertCallback ready_for_next_chunk,
                          bool end_stream) PURE;

  // Inserts trailers into the cache.
  virtual void insertTrailers(const Http::ResponseTrailerMap& trailers) PURE;

  virtual ~InsertContext() = default;
};
using InsertContextPtr = std::unique_ptr<InsertContext>;

// Lookup context manages the lifetime of a lookup, helping clients to pull data
// from the cache at a pace that works for them. At any time a client can abort
// an in-progress lookup by simply dropping the LookupContextPtr.
class LookupContext {
public:
  // Get the headers from the cache. It is a programming error to call this
  // twice.
  virtual void getHeaders(LookupHeadersCallback&& cb) PURE;

  // Reads the next chunk from the cache, calling cb when the chunk is ready.
  // The Buffer::InstancePtr passed to cb must not be null.
  //
  // The cache must call cb with a range of bytes starting at range.start() and
  // ending at or before range.end(). Caller is responsible for tracking what
  // ranges have been received, what to request next, and when to stop. A cache
  // can report an error, and cause the response to be aborted, by calling cb
  // with nullptr.
  //
  // If a cache happens to load data in chunks of a set size, it may be
  // efficient to respond with fewer than the requested number of bytes. For
  // example, assuming a 23 byte full-bodied response from a cache that reads in
  // absurdly small 10 byte chunks:
  //
  // getBody requests bytes  0-23 .......... callback with bytes 0-9
  // getBody requests bytes 10-23 .......... callback with bytes 10-19
  // getBody requests bytes 20-23 .......... callback with bytes 20-23
  virtual void getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb) PURE;

  // Get the trailers from the cache. Only called if LookupResult::has_trailers == true. The
  // Http::ResponseTrailerMapPtr passed to cb must not be null.
  virtual void getTrailers(LookupTrailersCallback&& cb) PURE;

  virtual ~LookupContext() = default;
};
using LookupContextPtr = std::unique_ptr<LookupContext>;

// Implement this interface to provide a cache implementation for use by
// CacheFilter.
class HttpCache {
public:
  // Returns a LookupContextPtr to manage the state of a cache lookup. On a cache
  // miss, the returned LookupContext will be given to the insert call (if any).
  virtual LookupContextPtr makeLookupContext(LookupRequest&& request) PURE;

  // Returns an InsertContextPtr to manage the state of a cache insertion.
  // Responses with a chunked transfer-encoding must be dechunked before
  // insertion.
  virtual InsertContextPtr makeInsertContext(LookupContextPtr&& lookup_context) PURE;

  // Precondition: lookup_context represents a prior cache lookup that required
  // validation.
  //
  // Update the headers of that cache entry to match response_headers. The cache
  // entry's body and trailers (if any) will not be modified.
  //
  // This is called when an expired cache entry is successfully validated, to
  // update the cache entry.
  virtual void updateHeaders(LookupContextPtr&& lookup_context,
                             Http::ResponseHeaderMapPtr&& response_headers) PURE;

  // Returns statically known information about a cache.
  virtual CacheInfo cacheInfo() const PURE;

  virtual ~HttpCache() = default;
};

// Factory interface for cache implementations to implement and register.
class HttpCacheFactory : public Config::TypedFactory {
public:
  // From UntypedFactory
  std::string category() const override { return "http_cache_factory"; }

  // Returns an HttpCache that will remain valid indefinitely (at least as long
  // as the calling CacheFilter).
  virtual HttpCache&
  getCache(const envoy::extensions::filters::http::cache::v3alpha::CacheConfig& config) PURE;
  virtual ~HttpCacheFactory() = default;

private:
  const std::string name_;
};
using HttpCacheFactoryPtr = std::unique_ptr<HttpCacheFactory>;
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
