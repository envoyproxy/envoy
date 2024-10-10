#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/time.h"
#include "envoy/config/typed_config.h"
#include "envoy/extensions/filters/http/cache/v3/cache.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/http/cache/cache_entry_utils.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/filters/http/cache/key.pb.h"
#include "source/extensions/filters/http/cache/range_utils.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

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
  // If the cache entry is still populating, and the cache supports streaming,
  // and the response had no content-length header, the content length may be
  // unknown at lookup-time.
  absl::optional<uint64_t> content_length_;

  // If the request is a range request, this struct indicates if the ranges can
  // be satisfied and which ranges are requested. nullopt indicates that this is
  // not a range request or the range header has been ignored.
  absl::optional<RangeDetails> range_details_;

  // Update the content length of the object and its response headers.
  void setContentLength(uint64_t new_length) {
    content_length_ = new_length;
    headers_->setContentLength(new_length);
  }
};
using LookupResultPtr = std::unique_ptr<LookupResult>;

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
size_t stableHashKey(const Key& key);

// LookupRequest holds everything about a request that's needed to look for a
// response in a cache, to evaluate whether an entry from a cache is usable, and
// to determine what ranges are needed.
class LookupRequest {
public:
  // Prereq: request_headers's Path(), Scheme(), and Host() are non-null.
  LookupRequest(const Http::RequestHeaderMap& request_headers, SystemTime timestamp,
                const VaryAllowList& vary_allow_list,
                bool ignore_request_cache_control_header = false);

  const RequestCacheControl& requestCacheControl() const { return request_cache_control_; }

  // Caches may modify the key according to local needs, though care must be
  // taken to ensure that meaningfully distinct responses have distinct keys.
  const Key& key() const { return key_; }

  // WARNING: Incomplete--do not use in production (yet).
  // Returns a LookupResult suitable for sending to the cache filter's
  // LookupHeadersCallback. Specifically,
  // - LookupResult::cache_entry_status_ is set according to HTTP cache
  // validation logic.
  // - LookupResult::headers_ takes ownership of response_headers.
  // - LookupResult::content_length_ == content_length.
  // - LookupResult::response_ranges_ entries are satisfiable (as documented
  // there).
  LookupResult makeLookupResult(Http::ResponseHeaderMapPtr&& response_headers,
                                ResponseMetadata&& metadata,
                                absl::optional<uint64_t> content_length) const;

  const Http::RequestHeaderMap& requestHeaders() const { return *request_headers_; }
  const VaryAllowList& varyAllowList() const { return vary_allow_list_; }

private:
  void initializeRequestCacheControl(const Http::RequestHeaderMap& request_headers);
  bool requiresValidation(const Http::ResponseHeaderMap& response_headers,
                          SystemTime::duration age) const;

  Key key_;
  std::vector<RawByteRange> request_range_spec_;
  Http::RequestHeaderMapPtr request_headers_;
  const VaryAllowList& vary_allow_list_;
  // Time when this LookupRequest was created (in response to an HTTP request).
  SystemTime timestamp_;
  RequestCacheControl request_cache_control_;
};

// Statically known information about a cache.
struct CacheInfo {
  absl::string_view name_;
  bool supports_range_requests_ = false;
};

using LookupBodyCallback = absl::AnyInvocable<void(Buffer::InstancePtr&&, bool end_stream)>;
using LookupHeadersCallback = absl::AnyInvocable<void(LookupResult&&, bool end_stream)>;
using LookupTrailersCallback = absl::AnyInvocable<void(Http::ResponseTrailerMapPtr&&)>;
using InsertCallback = absl::AnyInvocable<void(bool success_ready_for_more)>;
using UpdateHeadersCallback = absl::AnyInvocable<void(bool)>;

// Manages the lifetime of an insertion.
class InsertContext {
public:
  // Accepts response_headers for caching. Only called once.
  //
  // Implementations MUST post to the filter's dispatcher insert_complete(true)
  // on success, or insert_complete(false) to attempt to abort the insertion.
  // This call may be made asynchronously, but any async operation that can
  // potentially silently fail must include a timeout, to avoid memory leaks.
  virtual void insertHeaders(const Http::ResponseHeaderMap& response_headers,
                             const ResponseMetadata& metadata, InsertCallback insert_complete,
                             bool end_stream) PURE;

  // The insertion is streamed into the cache in fragments whose size is determined
  // by the client, but with a pace determined by the cache. To avoid streaming
  // data into cache too fast for the cache to handle, clients should wait for
  // the cache to call ready_for_next_fragment before sending the next fragment.
  //
  // The client can abort the streaming insertion by dropping the
  // InsertContextPtr. A cache can abort the insertion by passing 'false' into
  // ready_for_next_fragment.
  //
  // The cache implementation MUST post ready_for_next_fragment to the filter's
  // dispatcher. This post may be made asynchronously, but any async operation
  // that can potentially silently fail must include a timeout, to avoid memory leaks.
  virtual void insertBody(const Buffer::Instance& fragment, InsertCallback ready_for_next_fragment,
                          bool end_stream) PURE;

  // Inserts trailers into the cache.
  //
  // The cache implementation MUST post insert_complete to the filter's dispatcher.
  // This call may be made asynchronously, but any async operation that can
  // potentially silently fail must include a timeout, to avoid memory leaks.
  virtual void insertTrailers(const Http::ResponseTrailerMap& trailers,
                              InsertCallback insert_complete) PURE;

  // This routine is called prior to an InsertContext being destroyed. InsertContext is responsible
  // for making sure that any async activities are cleaned up before returning from onDestroy().
  // This includes timers, network calls, etc. The reason there is an onDestroy() method vs. doing
  // this type of cleanup in the destructor is to avoid potential data races between an async
  // callback and the destructor in case the connection terminates abruptly.
  // Example scenario with a hypothetical cache that uses RPC:
  // 1. [Filter's thread] CacheFilter calls InsertContext::insertBody.
  // 2. [Filter's thread] RPCInsertContext sends RPC and returns.
  // 3. [Filter's thread] Client disconnects; Destroying stream; CacheFilter destructor begins.
  // 4. [Filter's thread] RPCInsertContext destructor begins.
  // 5. [Other thread] RPC completes and calls RPCInsertContext::onRPCDone.
  // --> RPCInsertContext's destructor and onRpcDone cause a data race in RpcInsertContext.
  // onDestroy() should cancel any outstanding async operations and, if necessary,
  // it should block on that cancellation to avoid data races. InsertContext must not invoke any
  // callbacks to the CacheFilter after returning from onDestroy().
  virtual void onDestroy() PURE;

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
  // In the case that a cache supports shared streaming (serving content from
  // the cache entry while it is still being populated), and a range request is made
  // for a streaming entry that didn't have a content-length header from upstream, range
  // requests may be unable to receive a response until the content-length is
  // known to exceed the end of the requested range. In this case a cache
  // implementation should wait until that is known before calling the callback,
  // and must pass a LookupResult with range_details_->satisfiable_ = false
  // if the request is invalid.
  //
  // A cache that posts the callback must wrap it such that if the LookupContext is
  // destroyed before the callback is executed, the callback is not executed.
  virtual void getHeaders(LookupHeadersCallback&& cb) PURE;

  // Reads the next fragment from the cache, calling cb when the fragment is ready.
  // The Buffer::InstancePtr passed to cb must not be null.
  //
  // The cache must call cb with a range of bytes starting at range.start() and
  // ending at or before range.end(). Caller is responsible for tracking what
  // ranges have been received, what to request next, and when to stop.
  //
  // A request may have a range that exceeds the size of the content, in support
  // of a "shared stream" cache entry, where the request may not know the size of
  // the content in advance. In this case the cache should call cb with
  // end_stream=true when the end of the body is reached, if there are no trailers.
  //
  // If there are trailers *and* the size of the content was not known when the
  // LookupContext was created, the cache should pass a null buffer pointer to the
  // LookupBodyCallback (when getBody is called with a range starting beyond the
  // end of the actual content-length) to indicate that no more body is available
  // and the filter should request trailers. It is invalid to pass a null buffer
  // pointer other than in this case.
  //
  // If a cache happens to load data in fragments of a set size, it may be
  // efficient to respond with fewer than the requested number of bytes. For
  // example, assuming a 23 byte full-bodied response from a cache that reads in
  // absurdly small 10 byte fragments:
  //
  // getBody requests bytes  0-23 .......... callback with bytes 0-9
  // getBody requests bytes 10-23 .......... callback with bytes 10-19
  // getBody requests bytes 20-23 .......... callback with bytes 20-23
  //
  // A cache that posts the callback must wrap it such that if the LookupContext is
  // destroyed before the callback is executed, the callback is not executed.
  virtual void getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb) PURE;

  // Get the trailers from the cache. Only called if the request reached the end of
  // the body and LookupBodyCallback did not pass true for end_stream. The
  // Http::ResponseTrailerMapPtr passed to cb must not be null.
  //
  // A cache that posts the callback must wrap it such that if the LookupContext is
  // destroyed before the callback is executed, the callback is not executed.
  virtual void getTrailers(LookupTrailersCallback&& cb) PURE;

  // This routine is called prior to a LookupContext being destroyed. LookupContext is responsible
  // for making sure that any async activities are cleaned up before returning from onDestroy().
  // This includes timers, network calls, etc. The reason there is an onDestroy() method vs. doing
  // this type of cleanup in the destructor is to avoid potential data races between an async
  // callback and the destructor in case the connection terminates abruptly.
  // Example scenario with a hypothetical cache that uses RPC:
  // 1. [Filter's thread] CacheFilter calls LookupContext::getHeaders.
  // 2. [Filter's thread] RPCLookupContext sends RPC and returns.
  // 3. [Filter's thread] Client disconnects; Destroying stream; CacheFilter destructor begins.
  // 4. [Filter's thread] RPCLookupContext destructor begins.
  // 5. [Other thread] RPC completes and calls RPCLookupContext::onRPCDone.
  // --> RPCLookupContext's destructor and onRpcDone cause a data race in RPCLookupContext.
  // onDestroy() should cancel any outstanding async operations and, if necessary,
  // it should block on that cancellation to avoid data races. LookupContext must not invoke any
  // callbacks to the CacheFilter after having onDestroy() invoked.
  virtual void onDestroy() PURE;

  virtual ~LookupContext() = default;
};
using LookupContextPtr = std::unique_ptr<LookupContext>;

// Implement this interface to provide a cache implementation for use by
// CacheFilter.
class HttpCache {
public:
  // Returns a LookupContextPtr to manage the state of a cache lookup. On a cache
  // miss, the returned LookupContext will be given to the insert call (if any).
  //
  // It is possible for a cache to make a "shared stream" of responses allowing
  // read access to a cache entry before its write is complete. In this case the
  // content-length value may be unset.
  virtual LookupContextPtr makeLookupContext(LookupRequest&& request,
                                             Http::StreamFilterCallbacks& callbacks) PURE;

  // Returns an InsertContextPtr to manage the state of a cache insertion.
  // Responses with a chunked transfer-encoding must be dechunked before
  // insertion.
  virtual InsertContextPtr makeInsertContext(LookupContextPtr&& lookup_context,
                                             Http::StreamFilterCallbacks& callbacks) PURE;

  // Precondition: lookup_context represents a prior cache lookup that required
  // validation.
  //
  // Update the headers of that cache entry to match response_headers. The cache
  // entry's body and trailers (if any) will not be modified.
  //
  // This is called when an expired cache entry is successfully validated, to
  // update the cache entry.
  //
  // The on_complete callback is called with true if the update is successful,
  // false if the update was not performed.
  virtual void updateHeaders(const LookupContext& lookup_context,
                             const Http::ResponseHeaderMap& response_headers,
                             const ResponseMetadata& metadata,
                             UpdateHeadersCallback on_complete) PURE;

  // Returns statically known information about a cache.
  virtual CacheInfo cacheInfo() const PURE;

  virtual ~HttpCache() = default;
};

// Factory interface for cache implementations to implement and register.
class HttpCacheFactory : public Config::TypedFactory {
public:
  // From UntypedFactory
  std::string category() const override { return "envoy.http.cache"; }

  // Returns an HttpCache that will remain valid indefinitely (at least as long
  // as the calling CacheFilter).
  //
  // Pass factory context to allow HttpCache to use async client, stats scope
  // etc.
  virtual std::shared_ptr<HttpCache>
  getCache(const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
           Server::Configuration::FactoryContext& context) PURE;
  ~HttpCacheFactory() override = default;

private:
  const std::string name_;
};
using HttpCacheFactoryPtr = std::unique_ptr<HttpCacheFactory>;
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
