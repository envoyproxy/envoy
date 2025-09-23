#include "source/extensions/filters/http/cache_v2/cache_filter.h"

#include "envoy/http/header_map.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/cache_v2/cache_entry_utils.h"
#include "source/extensions/filters/http/cache_v2/cacheability_utils.h"
#include "source/extensions/filters/http/cache_v2/upstream_request_impl.h"

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

using CancelWrapper::cancelWrapped;

namespace {
// This value is only used if there is no encoderBufferLimit on the stream;
// without *some* constraint here, a very large chunk can be requested and
// attempt to load into a memory buffer.
//
// This default is quite large to minimize the chance of being a surprise
// behavioral change when a constraint is added.
//
// And everyone knows 64MB should be enough for anyone.
static constexpr size_t MaxBytesToFetchFromCachePerRead = 64 * 1024 * 1024;
} // namespace

namespace CacheResponseCodeDetails {
static constexpr absl::string_view ResponseFromCacheFilter = "cache.response_from_cache_filter";
static constexpr absl::string_view CacheFilterInsert = "cache.insert_via_upstream";
static constexpr absl::string_view CacheFilterAbortedDuringLookup = "cache.aborted_lookup";
static constexpr absl::string_view CacheFilterAbortedDuringHeaders = "cache.aborted_headers";
static constexpr absl::string_view CacheFilterAbortedDuringBody = "cache.aborted_body";
static constexpr absl::string_view CacheFilterAbortedDuringTrailers = "cache.aborted_trailers";
} // namespace CacheResponseCodeDetails

CacheFilterConfig::CacheFilterConfig(
    const envoy::extensions::filters::http::cache_v2::v3::CacheV2Config& config,
    std::shared_ptr<CacheSessions> cache_sessions,
    Server::Configuration::CommonFactoryContext& context)
    : vary_allow_list_(config.allowed_vary_headers(), context), time_source_(context.timeSource()),
      ignore_request_cache_control_header_(config.ignore_request_cache_control_header()),
      cluster_manager_(context.clusterManager()), cache_sessions_(std::move(cache_sessions)),
      override_upstream_cluster_(config.override_upstream_cluster()) {}

bool CacheFilterConfig::isCacheableResponse(const Http::ResponseHeaderMap& headers) const {
  return CacheabilityUtils::isCacheableResponse(headers, vary_allow_list_);
}

CacheFilter::CacheFilter(std::shared_ptr<const CacheFilterConfig> config) : config_(config) {}

void CacheFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks.addDownstreamWatermarkCallbacks(*this);
  PassThroughFilter::setDecoderFilterCallbacks(callbacks);
}

void CacheFilter::onDestroy() {
  is_destroyed_ = true;
  if (cancel_in_flight_callback_) {
    cancel_in_flight_callback_();
  }
  lookup_result_.reset();
}

absl::optional<absl::string_view> CacheFilter::clusterName() {
  Router::RouteConstSharedPtr route = decoder_callbacks_->route();
  const Router::RouteEntry* route_entry = (route == nullptr) ? nullptr : route->routeEntry();
  if (route_entry == nullptr) {
    return absl::nullopt;
  }
  return route_entry->clusterName();
}

OptRef<Http::AsyncClient> CacheFilter::asyncClient(absl::string_view cluster_name) {
  Upstream::ThreadLocalCluster* thread_local_cluster =
      config_->clusterManager().getThreadLocalCluster(cluster_name);
  if (thread_local_cluster == nullptr) {
    return absl::nullopt;
  }
  return thread_local_cluster->httpAsyncClient();
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

Http::FilterHeadersStatus CacheFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                     bool end_stream) {
  ASSERT(decoder_callbacks_);
  if (!config_->hasCache()) {
    return Http::FilterHeadersStatus::Continue;
  }
  if (!end_stream) {
    ENVOY_STREAM_LOG(debug,
                     "CacheFilter::decodeHeaders ignoring request because it has body and/or "
                     "trailers: headers={}",
                     *decoder_callbacks_, headers);
    stats().incForStatus(CacheEntryStatus::Uncacheable);
    return Http::FilterHeadersStatus::Continue;
  }
  absl::Status can_serve = CacheabilityUtils::canServeRequestFromCache(headers);
  if (!can_serve.ok()) {
    ENVOY_STREAM_LOG(debug,
                     "CacheFilter::decodeHeaders ignoring uncacheable request: {}\nheaders={}",
                     *decoder_callbacks_, can_serve, headers);
    stats().incForStatus(CacheEntryStatus::Uncacheable);
    return Http::FilterHeadersStatus::Continue;
  }
  ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders: {}", *decoder_callbacks_, headers);

  absl::optional<absl::string_view> original_cluster_name = clusterName();
  absl::string_view cluster_name;
  if (config_->overrideUpstreamCluster().empty()) {
    if (!original_cluster_name) {
      sendNoRouteResponse();
      return Http::FilterHeadersStatus::StopIteration;
    }
    cluster_name = *original_cluster_name;
  } else {
    cluster_name = config_->overrideUpstreamCluster();
    if (!original_cluster_name) {
      // It's possible the destination cluster will only be determined further upstream in
      // the cache filter's side-channel, in which case we can't use it in the key;
      // in this case use "unknown" instead.
      original_cluster_name = "unknown";
    }
  }
  OptRef<Http::AsyncClient> async_client = asyncClient(cluster_name);
  if (!async_client) {
    sendNoClusterResponse(cluster_name);
    return Http::FilterHeadersStatus::StopIteration;
  }
  auto upstream_request_factory = std::make_unique<UpstreamRequestImplFactory>(
      decoder_callbacks_->dispatcher(), *async_client, config_->upstreamOptions());
  auto lookup_request = std::make_unique<ActiveLookupRequest>(
      headers, std::move(upstream_request_factory), *original_cluster_name,
      decoder_callbacks_->dispatcher(), config_->timeSource().systemTime(), config_, config_,
      config_->ignoreRequestCacheControlHeader());
  is_head_request_ = headers.getMethodValue() == Http::Headers::get().MethodValues.Head;
  ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders starting lookup", *decoder_callbacks_);
  config_->cacheSessions().lookup(
      std::move(lookup_request),
      cancelWrapped(
          [this](ActiveLookupResultPtr lookup_result) { onLookupResult(std::move(lookup_result)); },
          &cancel_in_flight_callback_));

  // Stop the decoding stream.
  return Http::FilterHeadersStatus::StopIteration;
}

static absl::string_view responseCodeDetailsFromStatus(CacheEntryStatus status) {
  switch (status) {
  case CacheEntryStatus::Miss:
  case CacheEntryStatus::FailedValidation:
    return CacheResponseCodeDetails::CacheFilterInsert;
  case CacheEntryStatus::Hit:
  case CacheEntryStatus::FoundNotModified:
  case CacheEntryStatus::Follower:
  case CacheEntryStatus::Validated:
  case CacheEntryStatus::ValidatedFree:
  case CacheEntryStatus::UpstreamReset:
    return CacheResponseCodeDetails::ResponseFromCacheFilter;
  case CacheEntryStatus::Uncacheable:
  case CacheEntryStatus::LookupError:
    break;
  }
  return StreamInfo::ResponseCodeDetails::get().ViaUpstream;
}

void CacheFilter::onLookupResult(ActiveLookupResultPtr lookup_result) {
  ASSERT(lookup_result != nullptr, "lookup result should always be non-null");
  lookup_result_ = std::move(lookup_result);
  if (!lookup_result_->http_source_) {
    // Lookup failed, typically implying upstream request was reset.
    decoder_callbacks_->streamInfo().setResponseCodeDetails(
        CacheResponseCodeDetails::CacheFilterAbortedDuringLookup);
    decoder_callbacks_->resetStream();
    return;
  }

  stats().incForStatus(lookup_result_->status_);
  if (lookup_result_->status_ != CacheEntryStatus::Uncacheable) {
    decoder_callbacks_->streamInfo().setResponseFlag(
        StreamInfo::CoreResponseFlag::ResponseFromCacheFilter);
  }

  ENVOY_STREAM_LOG(debug, "CacheFilter calling getHeaders", *decoder_callbacks_);
  lookup_result_->http_source_->getHeaders(cancelWrapped(
      [this](Http::ResponseHeaderMapPtr response_headers, EndStream end_stream_enum) {
        onHeaders(std::move(response_headers), end_stream_enum);
      },
      &cancel_in_flight_callback_));
}

Http::FilterHeadersStatus CacheFilter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  if (lookup_result_) {
    // This call was invoked during decoding by decoder_callbacks_->encodeHeaders with data
    // either read from the upstream via the cache filter, or from the cache.
    return Http::FilterHeadersStatus::Continue;
  }
  if (!cancel_in_flight_callback_) {
    // If there was no lookup result and there's no request in flight, this implies
    // no request was sent, so we must be in a pass-through configuration (either no
    // cache or the request had a body).
    return Http::FilterHeadersStatus::Continue;
  }

  // Filter chain iteration is paused while a lookup is outstanding, but the filter chain manager
  // can still generate a local reply. One case where this can happen is when a downstream idle
  // timeout fires, which may mean that the HttpCache isn't correctly setting deadlines on its
  // asynchronous operations or is otherwise getting stuck.
  ENVOY_BUG(Http::Utility::getResponseStatus(headers) !=
                Envoy::enumToInt(Http::Code::RequestTimeout),
            "Request timed out while cache lookup was outstanding.");
  // Cancel the lookup since it's now not useful.
  ASSERT(cancel_in_flight_callback_);
  cancel_in_flight_callback_();
  return Http::FilterHeadersStatus::Continue;
}

void CacheFilter::getBody() {
  ASSERT(lookup_result_, "CacheFilter is trying to call getBody with no LookupResult");
  get_body_loop_ = GetBodyLoop::Again;
  while (get_body_loop_ == GetBodyLoop::Again) {
    ASSERT(!remaining_ranges_.empty(), "No reason to call getBody when there's no body to get.");

    // We don't want to request more than a buffer-size at a time from the cache.
    uint64_t fetch_size_limit = encoder_callbacks_->encoderBufferLimit();
    // If there is no buffer size limit, we still want *some* constraint.
    if (fetch_size_limit == 0) {
      fetch_size_limit = MaxBytesToFetchFromCachePerRead;
    }
    AdjustedByteRange fetch_range = {remaining_ranges_[0].begin(),
                                     (remaining_ranges_[0].length() > fetch_size_limit)
                                         ? (remaining_ranges_[0].begin() + fetch_size_limit)
                                         : remaining_ranges_[0].end()};

    ENVOY_STREAM_LOG(debug, "CacheFilter calling getBody", *decoder_callbacks_);
    get_body_loop_ = GetBodyLoop::InCallback;
    lookup_result_->http_source_->getBody(
        fetch_range, cancelWrapped(
                         [this, &dispatcher = decoder_callbacks_->dispatcher()](
                             Buffer::InstancePtr&& body, EndStream end_stream_enum) {
                           if (onBody(std::move(body), end_stream_enum)) {
                             if (get_body_loop_ == GetBodyLoop::InCallback) {
                               // If the callback was called inline, loop it.
                               get_body_loop_ = GetBodyLoop::Again;
                             } else {
                               // If the callback was posted we're not in the loop
                               // any more, so getBody to enter the loop.
                               getBody();
                             }
                           }
                         },
                         &cancel_in_flight_callback_));
  }
  get_body_loop_ = GetBodyLoop::Idle;
}

void CacheFilter::getTrailers() {
  ASSERT(lookup_result_, "CacheFilter is trying to call getTrailers with no LookupResult");

  lookup_result_->http_source_->getTrailers(cancelWrapped(
      [this, &dispatcher = decoder_callbacks_->dispatcher()](Http::ResponseTrailerMapPtr&& trailers,
                                                             EndStream end_stream_enum) {
        ASSERT(
            dispatcher.isThreadSafe(),
            "caches must ensure the callback is called from the original thread, either by posting "
            "to dispatcher or by calling directly");
        onTrailers(std::move(trailers), end_stream_enum);
      },
      &cancel_in_flight_callback_));
}

static AdjustedByteRange rangeFromHeaders(Http::ResponseHeaderMap& response_headers) {
  if (Http::Utility::getResponseStatus(response_headers) !=
      static_cast<uint64_t>(Envoy::Http::Code::PartialContent)) {
    // Don't use content-length; we can just request *all the body* from
    // the source and it will tell us when it gets to the end.
    return {0, std::numeric_limits<uint64_t>::max()};
  }
  Http::HeaderMap::GetResult content_range_result =
      response_headers.get(Envoy::Http::Headers::get().ContentRange);
  if (content_range_result.empty()) {
    return {0, std::numeric_limits<uint64_t>::max()};
  }
  absl::string_view content_range = content_range_result[0]->value().getStringView();
  if (!absl::ConsumePrefix(&content_range, "bytes ")) {
    return {0, std::numeric_limits<uint64_t>::max()};
  }
  if (absl::ConsumePrefix(&content_range, "*/")) {
    uint64_t len;
    if (absl::SimpleAtoi(content_range, &len)) {
      return {0, len};
    }
    return {0, std::numeric_limits<uint64_t>::max()};
  }
  std::pair<absl::string_view, absl::string_view> range_of = absl::StrSplit(content_range, '/');
  std::pair<absl::string_view, absl::string_view> range = absl::StrSplit(range_of.first, '-');
  uint64_t begin, end;
  if (!absl::SimpleAtoi(range.first, &begin)) {
    begin = 0;
  }
  if (!absl::SimpleAtoi(range.second, &end)) {
    end = std::numeric_limits<uint64_t>::max();
  } else {
    end++;
  }
  return {begin, end};
}

void CacheFilter::onHeaders(Http::ResponseHeaderMapPtr response_headers,
                            EndStream end_stream_enum) {
  ASSERT(lookup_result_, "onHeaders should not be called with no LookupResult");

  if (end_stream_enum == EndStream::Reset) {
    decoder_callbacks_->streamInfo().setResponseCodeDetails(
        CacheResponseCodeDetails::CacheFilterAbortedDuringHeaders);
    decoder_callbacks_->resetStream();
    return;
  }
  ASSERT(response_headers != nullptr);

  if (lookup_result_->status_ == CacheEntryStatus::Miss ||
      lookup_result_->status_ == CacheEntryStatus::Validated ||
      lookup_result_->status_ == CacheEntryStatus::ValidatedFree) {
    // CacheSessions adds an age header indiscriminately because once it has
    // handed off it doesn't remember which request is associated with the insert.
    // So here we remove that header for the non-cache response and the validated
    // response.
    response_headers->remove(Envoy::Http::CustomHeaders::get().Age);
  }

  static const std::string partial_content = std::to_string(enumToInt(Http::Code::PartialContent));
  if (response_headers->getStatusValue() == partial_content) {
    is_partial_response_ = true;
  }

  bool end_stream = ((end_stream_enum == EndStream::End) || is_head_request_);

  if (!end_stream) {
    remaining_ranges_ = {rangeFromHeaders(*response_headers)};
    ENVOY_STREAM_LOG(debug, "CacheFilter requesting range {}-{} {}", *decoder_callbacks_,
                     remaining_ranges_[0].begin(), remaining_ranges_[0].end(), *response_headers);
  }

  decoder_callbacks_->encodeHeaders(std::move(response_headers), end_stream,
                                    responseCodeDetailsFromStatus(lookup_result_->status_));
  // onDestroy can potentially be called during encodeHeaders.
  if (is_destroyed_) {
    return;
  }
  if (end_stream) {
    return;
  }
  return getBody();
}

bool CacheFilter::onBody(Buffer::InstancePtr&& body, EndStream end_stream_enum) {
  ASSERT(!remaining_ranges_.empty(),
         "CacheFilter doesn't call getBody unless there's more body to get, so this is a "
         "bogus callback.");
  if (end_stream_enum == EndStream::Reset) {
    decoder_callbacks_->streamInfo().setResponseCodeDetails(
        CacheResponseCodeDetails::CacheFilterAbortedDuringBody);
    decoder_callbacks_->resetStream();
    return false;
  }
  bool end_stream = end_stream_enum == EndStream::End;

  if (body == nullptr) {
    // if we called getBody and got a nullptr that implies there was less body
    // than expected, or we didn't have complete expectations.
    // It should not be treated as a bug here to have incorrect expectations,
    // as an untrusted upstream could send mismatched content-length and
    // body-stream.
    // If there is no body but there are trailers, this is how we know to
    // move on to trailers.
    if (end_stream) {
      Buffer::OwnedImpl empty_buffer;
      decoder_callbacks_->encodeData(empty_buffer, true);
      finalizeEncodingCachedResponse();
      return false;
    } else {
      getTrailers();
      return false;
    }
  }

  const uint64_t bytes_from_cache = body->length();
  if (bytes_from_cache < remaining_ranges_[0].length()) {
    remaining_ranges_[0].trimFront(bytes_from_cache);
  } else if (bytes_from_cache == remaining_ranges_[0].length()) {
    remaining_ranges_.erase(remaining_ranges_.begin());
  } else {
    decoder_callbacks_->resetStream();
    IS_ENVOY_BUG("Received oversized body from http source.");
    return false;
  }

  // For a range request the upstream may not have thought it was end_stream
  // but it still could be for the downstream.
  // This also covers the case where a range request wanted the last byte and
  // trailers are present; in this case we don't send trailers.
  // (It is unclear from the spec whether we should, but pragmatically we
  // may not have any indication of whether trailers are present or not, and
  // range requests in general are for filling in missing chunks so including
  // trailers with every chunk would be wasteful.)
  if (is_partial_response_ && remaining_ranges_.empty()) {
    end_stream = true;
  }

  decoder_callbacks_->encodeData(*body, end_stream);
  // Filter can potentially be destroyed during encodeData (e.g. if
  // encodeData provokes a reset)
  if (is_destroyed_) {
    return false;
  }

  if (end_stream) {
    finalizeEncodingCachedResponse();
    return false;
  } else if (!remaining_ranges_.empty()) {
    if (downstream_watermarked_) {
      get_body_on_unblocked_ = true;
      return false;
    } else {
      return true;
    }
  } else {
    getTrailers();
    return false;
  }
}

void CacheFilter::onAboveWriteBufferHighWatermark() { downstream_watermarked_++; }

void CacheFilter::onBelowWriteBufferLowWatermark() {
  if (downstream_watermarked_ == 0) {
    IS_ENVOY_BUG("low watermark not preceded by high watermark should not happen");
  } else {
    downstream_watermarked_--;
  }
  if (downstream_watermarked_ == 0 && get_body_on_unblocked_) {
    get_body_on_unblocked_ = false;
    getBody();
  }
}

void CacheFilter::onTrailers(Http::ResponseTrailerMapPtr&& trailers, EndStream end_stream_enum) {
  ASSERT(!is_destroyed_, "callback should be cancelled when filter is destroyed");
  if (end_stream_enum == EndStream::Reset) {
    decoder_callbacks_->streamInfo().setResponseCodeDetails(
        CacheResponseCodeDetails::CacheFilterAbortedDuringTrailers);
    decoder_callbacks_->resetStream();
    return;
  }
  decoder_callbacks_->encodeTrailers(std::move(trailers));
  // Filter can potentially be destroyed during encodeTrailers.
  if (is_destroyed_) {
    return;
  }
  finalizeEncodingCachedResponse();
}

void CacheFilter::finalizeEncodingCachedResponse() {}

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
