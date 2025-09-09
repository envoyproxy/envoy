#include "source/extensions/filters/http/cache/cache_filter.h"

#include "envoy/http/header_map.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/cache/cache_custom_headers.h"
#include "source/extensions/filters/http/cache/cache_entry_utils.h"
#include "source/extensions/filters/http/cache/cache_filter_logging_info.h"
#include "source/extensions/filters/http/cache/cacheability_utils.h"
#include "source/extensions/filters/http/cache/upstream_request.h"

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

namespace {
// This value is only used if there is no encoderBufferLimit on the stream;
// without *some* constraint here, a very large chunk can be requested and
// attempt to load into a memory buffer.
//
// This default is quite large to minimize the chance of being a surprise
// behavioral change when a constraint is added.
//
// And everyone knows 64MB should be enough for anyone.
static const size_t MAX_BYTES_TO_FETCH_FROM_CACHE_PER_REQUEST = 64 * 1024 * 1024;
} // namespace

struct CacheResponseCodeDetailValues {
  const absl::string_view ResponseFromCacheFilter = "cache.response_from_cache_filter";
};

using CacheResponseCodeDetails = ConstSingleton<CacheResponseCodeDetailValues>;

CacheFilterConfig::CacheFilterConfig(
    const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
    Server::Configuration::CommonFactoryContext& context)
    : vary_allow_list_(config.allowed_vary_headers(), context), time_source_(context.timeSource()),
      ignore_request_cache_control_header_(config.ignore_request_cache_control_header()),
      cluster_manager_(context.clusterManager()) {}

CacheFilter::CacheFilter(std::shared_ptr<const CacheFilterConfig> config,
                         std::shared_ptr<HttpCache> http_cache)
    : cache_(http_cache), config_(config) {}

void CacheFilter::onDestroy() {
  filter_state_ = FilterState::Destroyed;
  if (lookup_ != nullptr) {
    lookup_->onDestroy();
  }
  if (upstream_request_ != nullptr) {
    upstream_request_->disconnectFilter();
    upstream_request_ = nullptr;
  }
}

void CacheFilter::sendUpstreamRequest(Http::RequestHeaderMap& request_headers) {
  Router::RouteConstSharedPtr route = decoder_callbacks_->route();
  const Router::RouteEntry* route_entry = (route == nullptr) ? nullptr : route->routeEntry();
  if (route_entry == nullptr) {
    return sendNoRouteResponse();
  }
  Upstream::ThreadLocalCluster* thread_local_cluster =
      config_->clusterManager().getThreadLocalCluster(route_entry->clusterName());
  if (thread_local_cluster == nullptr) {
    return sendNoClusterResponse(route_entry->clusterName());
  }
  upstream_request_ =
      UpstreamRequest::create(this, std::move(lookup_), std::move(lookup_result_), cache_,
                              thread_local_cluster->httpAsyncClient(), config_->upstreamOptions());
  upstream_request_->sendHeaders(request_headers);
}

void CacheFilter::sendNoRouteResponse() {
  decoder_callbacks_->sendLocalReply(Http::Code::NotFound, "", nullptr, absl::nullopt,
                                     "cache_no_route");
}

void CacheFilter::sendNoClusterResponse(absl::string_view cluster_name) {
  ENVOY_STREAM_LOG(debug, "upstream cluster '{}' was not available to cache", *decoder_callbacks_,
                   cluster_name);
  decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable, "", nullptr, absl::nullopt,
                                     "cache_no_cluster");
}

void CacheFilter::onStreamComplete() {
  LookupStatus lookup_status = lookupStatus();
  InsertStatus insert_status = insertStatus();
  decoder_callbacks_->streamInfo().filterState()->setData(
      CacheFilterLoggingInfo::FilterStateKey,
      std::make_shared<CacheFilterLoggingInfo>(lookup_status, insert_status),
      StreamInfo::FilterState::StateType::ReadOnly);
}

Http::FilterHeadersStatus CacheFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                     bool end_stream) {
  if (!cache_) {
    filter_state_ = FilterState::NotServingFromCache;
    return Http::FilterHeadersStatus::Continue;
  }
  ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders: {}", *decoder_callbacks_, headers);
  if (!end_stream) {
    ENVOY_STREAM_LOG(
        debug,
        "CacheFilter::decodeHeaders ignoring request because it has body and/or trailers: {}",
        *decoder_callbacks_, headers);
    filter_state_ = FilterState::NotServingFromCache;
    return Http::FilterHeadersStatus::Continue;
  }
  if (!CacheabilityUtils::canServeRequestFromCache(headers)) {
    ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders ignoring uncacheable request: {}",
                     *decoder_callbacks_, headers);
    filter_state_ = FilterState::NotServingFromCache;
    insert_status_ = InsertStatus::NoInsertRequestNotCacheable;
    return Http::FilterHeadersStatus::Continue;
  }
  ASSERT(decoder_callbacks_);

  LookupRequest lookup_request(headers, config_->timeSource().systemTime(),
                               config_->varyAllowList(),
                               config_->ignoreRequestCacheControlHeader());
  request_allows_inserts_ = !lookup_request.requestCacheControl().no_store_;
  is_head_request_ = headers.getMethodValue() == Http::Headers::get().MethodValues.Head;
  lookup_ = cache_->makeLookupContext(std::move(lookup_request), *decoder_callbacks_);

  ASSERT(lookup_);
  getHeaders(headers);
  ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders starting lookup", *decoder_callbacks_);

  // Stop the decoding stream until the cache lookup result is ready.
  return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
}

void CacheFilter::onUpstreamRequestComplete() { upstream_request_ = nullptr; }

void CacheFilter::onUpstreamRequestReset() {
  upstream_request_ = nullptr;
  decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable, "", nullptr, absl::nullopt,
                                     "cache_upstream_reset");
}

Http::FilterHeadersStatus CacheFilter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  if (filter_state_ == FilterState::ServingFromCache) {
    // This call was invoked during decoding by decoder_callbacks_->encodeHeaders because a fresh
    // cached response was found and is being added to the encoding stream -- ignore it.
    return Http::FilterHeadersStatus::Continue;
  }

  // If lookup_ is null, the request wasn't cacheable, so the response isn't either.
  if (!lookup_) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (lookup_result_ == nullptr) {
    // Filter chain iteration is paused while a lookup is outstanding, but the filter chain manager
    // can still generate a local reply. One case where this can happen is when a downstream idle
    // timeout fires, which may mean that the HttpCache isn't correctly setting deadlines on its
    // asynchronous operations or is otherwise getting stuck.
    ENVOY_BUG(Http::Utility::getResponseStatus(headers) !=
                  Envoy::enumToInt(Http::Code::RequestTimeout),
              "Request timed out while cache lookup was outstanding.");
    // Cancel the lookup since it's now not useful.
    lookup_->onDestroy();
    lookup_ = nullptr;
    return Http::FilterHeadersStatus::Continue;
  }

  IS_ENVOY_BUG("encodeHeaders should not be called except under the conditions handled above");
  return Http::FilterHeadersStatus::Continue;
}

/*static*/ LookupStatus
CacheFilter::resolveLookupStatus(absl::optional<CacheEntryStatus> cache_entry_status,
                                 FilterState filter_state) {
  if (cache_entry_status.has_value()) {
    switch (cache_entry_status.value()) {
    case CacheEntryStatus::Ok:
      return LookupStatus::CacheHit;
    case CacheEntryStatus::Unusable:
      return LookupStatus::CacheMiss;
    case CacheEntryStatus::RequiresValidation: {
      // The CacheFilter sent the response upstream for validation; check the
      // filter state to see whether and how the upstream responded. The
      // filter currently won't send the stale entry if it can't reach the
      // upstream or if the upstream responds with a 5xx, so don't include
      // special handling for those cases.
      switch (filter_state) {
      case FilterState::ValidatingCachedResponse:
        return LookupStatus::RequestIncomplete;
      case FilterState::ServingFromCache:
        ABSL_FALLTHROUGH_INTENDED;
      case FilterState::ResponseServedFromCache:
        // Functionally a cache hit, this is differentiated for metrics reporting.
        return LookupStatus::StaleHitWithSuccessfulValidation;
      case FilterState::NotServingFromCache:
        return LookupStatus::StaleHitWithFailedValidation;
      case FilterState::Initial:
        ABSL_FALLTHROUGH_INTENDED;
      case FilterState::Destroyed:
        IS_ENVOY_BUG(absl::StrCat("Unexpected filter state in requestCacheStatus: cache lookup "
                                  "response required validation, but filter state is ",
                                  filter_state));
      }
      return LookupStatus::Unknown;
    }
    case CacheEntryStatus::FoundNotModified:
      // TODO(capoferro): Report this as a FoundNotModified when we handle
      // those.
      return LookupStatus::CacheHit;
    case CacheEntryStatus::LookupError:
      return LookupStatus::LookupError;
    }
    IS_ENVOY_BUG(absl::StrCat(
        "Unhandled CacheEntryStatus encountered when retrieving request cache status: " +
        std::to_string(static_cast<int>(filter_state))));
    return LookupStatus::Unknown;
  }
  // Either decodeHeaders decided not to do a cache lookup (because the
  // request isn't cacheable), or decodeHeaders hasn't been called yet.
  switch (filter_state) {
  case FilterState::Initial:
    return LookupStatus::RequestIncomplete;
  case FilterState::NotServingFromCache:
    return LookupStatus::RequestNotCacheable;
  // Ignore the following lines. This code should not be executed.
  // GCOV_EXCL_START
  case FilterState::ValidatingCachedResponse:
    ABSL_FALLTHROUGH_INTENDED;
  case FilterState::ServingFromCache:
    ABSL_FALLTHROUGH_INTENDED;
  case FilterState::ResponseServedFromCache:
    ABSL_FALLTHROUGH_INTENDED;
  case FilterState::Destroyed:
    ENVOY_LOG(error, absl::StrCat("Unexpected filter state in requestCacheStatus: "
                                  "lookup_result_ is empty but filter state is ",
                                  filter_state));
  }
  return LookupStatus::Unknown;
}

void CacheFilter::getHeaders(Http::RequestHeaderMap& request_headers) {
  ASSERT(lookup_, "CacheFilter is trying to call getHeaders with no LookupContext");
  callback_called_directly_ = true;
  lookup_->getHeaders([this, &request_headers, &dispatcher = decoder_callbacks_->dispatcher()](
                          LookupResult&& result, bool end_stream) {
    ASSERT(!callback_called_directly_ && dispatcher.isThreadSafe(),
           "caches must post the callback to the filter's dispatcher");
    onHeaders(std::move(result), request_headers, end_stream);
  });
  callback_called_directly_ = false;
}

void CacheFilter::getBody() {
  ASSERT(lookup_, "CacheFilter is trying to call getBody with no LookupContext");
  ASSERT(!remaining_ranges_.empty(), "No reason to call getBody when there's no body to get.");

  // We don't want to request more than a buffer-size at a time from the cache.
  uint64_t fetch_size_limit = encoder_callbacks_->encoderBufferLimit();
  // If there is no buffer size limit, we still want *some* constraint.
  if (fetch_size_limit == 0) {
    fetch_size_limit = MAX_BYTES_TO_FETCH_FROM_CACHE_PER_REQUEST;
  }
  AdjustedByteRange fetch_range = {remaining_ranges_[0].begin(),
                                   (remaining_ranges_[0].length() > fetch_size_limit)
                                       ? (remaining_ranges_[0].begin() + fetch_size_limit)
                                       : remaining_ranges_[0].end()};

  callback_called_directly_ = true;
  lookup_->getBody(fetch_range, [this, &dispatcher = decoder_callbacks_->dispatcher()](
                                    Buffer::InstancePtr&& body, bool end_stream) {
    ASSERT(!callback_called_directly_ && dispatcher.isThreadSafe(),
           "caches must post the callback to the filter's dispatcher");
    onBody(std::move(body), end_stream);
  });
  callback_called_directly_ = false;
}

void CacheFilter::getTrailers() {
  ASSERT(lookup_, "CacheFilter is trying to call getTrailers with no LookupContext");

  callback_called_directly_ = true;
  lookup_->getTrailers([this, &dispatcher = decoder_callbacks_->dispatcher()](
                           Http::ResponseTrailerMapPtr&& trailers) {
    ASSERT(!callback_called_directly_ && dispatcher.isThreadSafe(),
           "caches must post the callback to the filter's dispatcher");
    onTrailers(std::move(trailers));
  });
  callback_called_directly_ = false;
}

void CacheFilter::onHeaders(LookupResult&& result, Http::RequestHeaderMap& request_headers,
                            bool end_stream) {
  if (filter_state_ == FilterState::Destroyed) {
    // The filter is being destroyed, any callbacks should be ignored.
    return;
  }
  if (filter_state_ == FilterState::NotServingFromCache) {
    // A response was injected into the filter chain before the cache lookup finished, e.g. because
    // the request stream timed out.
    return;
  }

  // TODO(yosrym93): Handle request only-if-cached directive
  lookup_result_ = std::make_unique<LookupResult>(std::move(result));
  cache_entry_status_ = lookup_result_->cache_entry_status_;
  switch (cache_entry_status_.value()) {
  case CacheEntryStatus::FoundNotModified:
    PANIC("unsupported code");
  case CacheEntryStatus::RequiresValidation:
    // If a cache entry requires validation, inject validation headers in the
    // request and let it pass through as if no cache entry was found. If the
    // cache entry was valid, the response status should be 304 (unmodified)
    // and the cache entry will be injected in the response body.
    handleCacheHitWithValidation(request_headers);
    return;
  case CacheEntryStatus::Ok:
    if (lookup_result_->range_details_.has_value()) {
      handleCacheHitWithRangeRequest();
      return;
    }
    handleCacheHit(/* end_stream_after_headers = */ end_stream);
    return;
  case CacheEntryStatus::Unusable:
    sendUpstreamRequest(request_headers);
    return;
  case CacheEntryStatus::LookupError:
    filter_state_ = FilterState::NotServingFromCache;
    insert_status_ = InsertStatus::NoInsertLookupError;
    decoder_callbacks_->continueDecoding();
    return;
  }
  ENVOY_LOG(error, "Unhandled CacheEntryStatus in CacheFilter::onHeaders: {}",
            cacheEntryStatusString(cache_entry_status_.value()));
  // Treat unhandled status as a cache miss.
  sendUpstreamRequest(request_headers);
}

// TODO(toddmgreer): Handle downstream backpressure.
void CacheFilter::onBody(Buffer::InstancePtr&& body, bool end_stream) {
  // Can be called during decoding if a valid cache hit is found,
  // or during encoding if a cache entry was being validated.
  if (filter_state_ == FilterState::Destroyed) {
    // The filter is being destroyed, any callbacks should be ignored.
    return;
  }
  ASSERT(!remaining_ranges_.empty(),
         "CacheFilter doesn't call getBody unless there's more body to get, so this is a "
         "bogus callback.");
  if (remaining_ranges_[0].end() == std::numeric_limits<uint64_t>::max() && body == nullptr) {
    ASSERT(!end_stream);
    getTrailers();
    return;
  }
  ASSERT(body, "Cache said it had a body, but isn't giving it to us.");

  const uint64_t bytes_from_cache = body->length();
  if (bytes_from_cache < remaining_ranges_[0].length()) {
    remaining_ranges_[0].trimFront(bytes_from_cache);
  } else if (bytes_from_cache == remaining_ranges_[0].length()) {
    remaining_ranges_.erase(remaining_ranges_.begin());
  } else {
    ASSERT(false, "Received oversized body from cache.");
    decoder_callbacks_->resetStream();
    return;
  }

  decoder_callbacks_->encodeData(*body, end_stream);

  if (end_stream) {
    finalizeEncodingCachedResponse();
  } else if (!remaining_ranges_.empty()) {
    getBody();
  } else if (lookup_result_->range_details_.has_value()) {
    // If a range was requested we don't send trailers.
    // (It is unclear from the spec whether we should, but pragmatically we
    // don't have any indication of whether trailers are present or not, and
    // range requests in general are for filling in missing chunks so including
    // trailers with every chunk would be wasteful.)
    finalizeEncodingCachedResponse();
  } else {
    getTrailers();
  }
}

void CacheFilter::onTrailers(Http::ResponseTrailerMapPtr&& trailers) {
  // Can be called during decoding if a valid cache hit is found,
  // or during encoding if a cache entry was being validated.
  if (filter_state_ == FilterState::Destroyed) {
    // The filter is being destroyed, any callbacks should be ignored.
    return;
  }
  decoder_callbacks_->encodeTrailers(std::move(trailers));
  // Filter can potentially be destroyed during encodeTrailers.
  if (filter_state_ == FilterState::Destroyed) {
    return;
  }
  finalizeEncodingCachedResponse();
}

void CacheFilter::handleCacheHit(bool end_stream_after_headers) {
  filter_state_ = FilterState::ServingFromCache;
  insert_status_ = InsertStatus::NoInsertCacheHit;
  encodeCachedResponse(end_stream_after_headers);
}

void CacheFilter::handleCacheHitWithRangeRequest() {
  if (!lookup_result_->range_details_.has_value()) {
    ENVOY_LOG(error, "handleCacheHitWithRangeRequest() should not be called without "
                     "range_details_ being populated in lookup_result_");
    return;
  }
  if (!lookup_result_->range_details_->satisfiable_) {
    filter_state_ = FilterState::ServingFromCache;
    insert_status_ = InsertStatus::NoInsertCacheHit;
    lookup_result_->headers_->setStatus(
        static_cast<uint64_t>(Envoy::Http::Code::RangeNotSatisfiable));
    if (lookup_result_->content_length_.has_value()) {
      lookup_result_->headers_->addCopy(
          Envoy::Http::Headers::get().ContentRange,
          absl::StrCat("bytes */", lookup_result_->content_length_.value()));
    } else {
      IS_ENVOY_BUG(
          "handleCacheHitWithRangeRequest() should not be called with satisfiable_=false "
          "without content_length_ being populated in lookup_result_. Cache implementation "
          "should wait to respond to getHeaders in this case until content_length_ is known, "
          "declaring a miss, or should strip range_details_ from the lookup result.");
    }
    // We shouldn't serve any of the body, so the response content length
    // is 0.
    lookup_result_->setContentLength(0);
    encodeCachedResponse(/* end_stream_after_headers = */ true);
    return;
  }

  std::vector<AdjustedByteRange> ranges = lookup_result_->range_details_->ranges_;
  if (ranges.size() != 1) {
    // Multi-part responses are not supported, and they will be treated as
    // a usual 200 response. A possible way to achieve that would be to move
    // all ranges to remaining_ranges_, and add logic inside '::onBody' to
    // interleave the body bytes with sub-headers and separator string for
    // each part. Would need to keep track if the current range is over or
    // not to know when to insert the separator, and calculate the length
    // based on length of ranges + extra headers and separators.
    handleCacheHit(/* end_stream_after_headers = */ false);
    return;
  }

  filter_state_ = FilterState::ServingFromCache;
  insert_status_ = InsertStatus::NoInsertCacheHit;

  lookup_result_->headers_->setStatus(static_cast<uint64_t>(Envoy::Http::Code::PartialContent));
  lookup_result_->headers_->addCopy(
      Envoy::Http::Headers::get().ContentRange,
      absl::StrCat("bytes ", ranges[0].begin(), "-", ranges[0].end() - 1, "/",
                   lookup_result_->content_length_.has_value()
                       ? absl::StrCat(lookup_result_->content_length_.value())
                       : "*"));
  // We serve only the desired range, so adjust the length
  // accordingly.
  lookup_result_->setContentLength(ranges[0].length());
  remaining_ranges_ = std::move(ranges);
  encodeCachedResponse(/* end_stream_after_headers = */ false);
}

void CacheFilter::handleCacheHitWithValidation(Envoy::Http::RequestHeaderMap& request_headers) {
  filter_state_ = FilterState::ValidatingCachedResponse;
  injectValidationHeaders(request_headers);
  sendUpstreamRequest(request_headers);
}

void CacheFilter::injectValidationHeaders(Http::RequestHeaderMap& request_headers) {
  ASSERT(lookup_result_, "injectValidationHeaders precondition unsatisfied: lookup_result_ "
                         "does not point to a cache lookup result");
  ASSERT(filter_state_ == FilterState::ValidatingCachedResponse,
         "injectValidationHeaders precondition unsatisfied: the "
         "CacheFilter is not validating a cache lookup result");

  const Http::HeaderEntry* etag_header =
      lookup_result_->headers_->getInline(CacheCustomHeaders::etag());
  const Http::HeaderEntry* last_modified_header =
      lookup_result_->headers_->getInline(CacheCustomHeaders::lastModified());

  if (etag_header) {
    absl::string_view etag = etag_header->value().getStringView();
    request_headers.setInline(CacheCustomHeaders::ifNoneMatch(), etag);
  }
  if (DateUtil::timePointValid(CacheHeadersUtils::httpTime(last_modified_header))) {
    // Valid Last-Modified header exists.
    absl::string_view last_modified = last_modified_header->value().getStringView();
    request_headers.setInline(CacheCustomHeaders::ifModifiedSince(), last_modified);
  } else {
    // Either Last-Modified is missing or invalid, fallback to Date.
    // A correct behaviour according to:
    // https://httpwg.org/specs/rfc7232.html#header.if-modified-since
    absl::string_view date = lookup_result_->headers_->getDateValue();
    request_headers.setInline(CacheCustomHeaders::ifModifiedSince(), date);
  }
}

void CacheFilter::encodeCachedResponse(bool end_stream_after_headers) {
  ASSERT(lookup_result_, "encodeCachedResponse precondition unsatisfied: lookup_result_ "
                         "does not point to a cache lookup result");

  // Set appropriate response flags and codes.
  decoder_callbacks_->streamInfo().setResponseFlag(
      StreamInfo::CoreResponseFlag::ResponseFromCacheFilter);
  decoder_callbacks_->streamInfo().setResponseCodeDetails(
      CacheResponseCodeDetails::get().ResponseFromCacheFilter);

  decoder_callbacks_->encodeHeaders(std::move(lookup_result_->headers_),
                                    is_head_request_ || end_stream_after_headers,
                                    CacheResponseCodeDetails::get().ResponseFromCacheFilter);
  // Filter can potentially be destroyed during encodeHeaders.
  if (filter_state_ == FilterState::Destroyed) {
    return;
  }
  if (is_head_request_ || end_stream_after_headers) {
    filter_state_ = FilterState::ResponseServedFromCache;
    return;
  }
  if (remaining_ranges_.empty() && lookup_result_->content_length_.value_or(1) > 0) {
    // No range has been added, so we add entire body to the response.
    remaining_ranges_.emplace_back(
        0, lookup_result_->content_length_.value_or(std::numeric_limits<uint64_t>::max()));
  }
  if (!remaining_ranges_.empty()) {
    getBody();
  } else {
    getTrailers();
  }
}

void CacheFilter::finalizeEncodingCachedResponse() {
  filter_state_ = FilterState::ResponseServedFromCache;
}

LookupStatus CacheFilter::lookupStatus() const {
  if (lookup_result_ == nullptr && lookup_ != nullptr) {
    return LookupStatus::RequestIncomplete;
  }

  return resolveLookupStatus(cache_entry_status_, filter_state_);
}

InsertStatus CacheFilter::insertStatus() const {
  return insert_status_.value_or((upstream_request_ == nullptr)
                                     ? InsertStatus::NoInsertRequestIncomplete
                                     : InsertStatus::FilterAbortedBeforeInsertComplete);
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
