#include "source/extensions/filters/http/cache/upstream_request.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/cache/cache_custom_headers.h"
#include "source/extensions/filters/http/cache/cache_filter.h"
#include "source/extensions/filters/http/cache/cacheability_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

namespace {
inline bool isResponseNotModified(const Http::ResponseHeaderMap& response_headers) {
  return Http::Utility::getResponseStatus(response_headers) == enumToInt(Http::Code::NotModified);
}
} // namespace

void UpstreamRequest::setFilterState(FilterState fs) {
  filter_state_ = fs;
  if (filter_ != nullptr && filter_->filter_state_ != FilterState::Destroyed) {
    filter_->filter_state_ = fs;
  }
}

void UpstreamRequest::setInsertStatus(InsertStatus is) {
  if (filter_ != nullptr && filter_->filter_state_ != FilterState::Destroyed) {
    filter_->insert_status_ = is;
  }
}

void UpstreamRequest::processSuccessfulValidation(Http::ResponseHeaderMapPtr response_headers) {
  ASSERT(lookup_result_, "CacheFilter trying to validate a non-existent lookup result");
  ASSERT(
      filter_state_ == FilterState::ValidatingCachedResponse,
      "processSuccessfulValidation must only be called when a cached response is being validated");
  ASSERT(isResponseNotModified(*response_headers),
         "processSuccessfulValidation must only be called with 304 responses");

  // Check whether the cached entry should be updated before modifying the 304 response.
  const bool should_update_cached_entry = shouldUpdateCachedEntry(*response_headers);

  setFilterState(FilterState::ServingFromCache);

  // Replace the 304 response status code with the cached status code.
  response_headers->setStatus(lookup_result_->headers_->getStatusValue());

  // Remove content length header if the 304 had one; if the cache entry had a
  // content length header it will be added by the header adding block below.
  response_headers->removeContentLength();

  // A response that has been validated should not contain an Age header as it is equivalent to a
  // freshly served response from the origin, unless the 304 response has an Age header, which
  // means it was served by an upstream cache.
  // Remove any existing Age header in the cached response.
  lookup_result_->headers_->removeInline(CacheCustomHeaders::age());

  // Add any missing headers from the cached response to the 304 response.
  lookup_result_->headers_->iterate([&response_headers](const Http::HeaderEntry& cached_header) {
    // TODO(yosrym93): Try to avoid copying the header key twice.
    Http::LowerCaseString key(cached_header.key().getStringView());
    absl::string_view value = cached_header.value().getStringView();
    if (response_headers->get(key).empty()) {
      response_headers->setCopy(key, value);
    }
    return Http::HeaderMap::Iterate::Continue;
  });

  if (should_update_cached_entry) {
    // TODO(yosrym93): else the cached entry should be deleted.
    // Update metadata associated with the cached response. Right now this is only response_time;
    const ResponseMetadata metadata = {config_->timeSource().systemTime()};
    cache_->updateHeaders(*lookup_, *response_headers, metadata,
                          [](bool updated ABSL_ATTRIBUTE_UNUSED) {});
    setInsertStatus(InsertStatus::HeaderUpdate);
  }

  // A cache entry was successfully validated, so abort the upstream request, send
  // encode the merged-modified headers, and encode cached body and trailers.
  if (filter_ != nullptr) {
    lookup_result_->headers_ = std::move(response_headers);
    filter_->lookup_result_ = std::move(lookup_result_);
    filter_->lookup_ = std::move(lookup_);
    filter_->upstream_request_ = nullptr;
    lookup_result_ = nullptr;
    filter_->encodeCachedResponse(/* end_stream_after_headers = */ false);
    filter_ = nullptr;
    abort();
  }
}

// TODO(yosrym93): Write a test that exercises this when SimpleHttpCache implements updateHeaders
bool UpstreamRequest::shouldUpdateCachedEntry(
    const Http::ResponseHeaderMap& response_headers) const {
  ASSERT(isResponseNotModified(response_headers),
         "shouldUpdateCachedEntry must only be called with 304 responses");
  ASSERT(lookup_result_, "shouldUpdateCachedEntry precondition unsatisfied: lookup_result_ "
                         "does not point to a cache lookup result");
  ASSERT(filter_state_ == FilterState::ValidatingCachedResponse,
         "shouldUpdateCachedEntry precondition unsatisfied: the "
         "CacheFilter is not validating a cache lookup result");

  // According to: https://httpwg.org/specs/rfc7234.html#freshening.responses,
  // and assuming a single cached response per key:
  // If the 304 response contains a strong validator (etag) that does not match the cached response,
  // the cached response should not be updated.
  const Http::HeaderEntry* response_etag = response_headers.getInline(CacheCustomHeaders::etag());
  const Http::HeaderEntry* cached_etag =
      lookup_result_->headers_->getInline(CacheCustomHeaders::etag());
  return !response_etag || (cached_etag && cached_etag->value().getStringView() ==
                                               response_etag->value().getStringView());
}

UpstreamRequest* UpstreamRequest::create(CacheFilter* filter, LookupContextPtr lookup,
                                         LookupResultPtr lookup_result,
                                         std::shared_ptr<HttpCache> cache,
                                         Http::AsyncClient& async_client,
                                         const Http::AsyncClient::StreamOptions& options) {
  return new UpstreamRequest(filter, std::move(lookup), std::move(lookup_result), std::move(cache),
                             async_client, options);
}

UpstreamRequest::UpstreamRequest(CacheFilter* filter, LookupContextPtr lookup,
                                 LookupResultPtr lookup_result, std::shared_ptr<HttpCache> cache,
                                 Http::AsyncClient& async_client,
                                 const Http::AsyncClient::StreamOptions& options)
    : filter_(filter), lookup_(std::move(lookup)), lookup_result_(std::move(lookup_result)),
      is_head_request_(filter->is_head_request_),
      request_allows_inserts_(filter->request_allows_inserts_), config_(filter->config_),
      filter_state_(filter->filter_state_), cache_(std::move(cache)),
      stream_(async_client.start(*this, options)) {
  ASSERT(stream_ != nullptr);
}

void UpstreamRequest::insertQueueOverHighWatermark() {
  // TODO(ravenblack): currently AsyncRequest::Stream does not support pausing.
}

void UpstreamRequest::insertQueueUnderLowWatermark() {
  // TODO(ravenblack): currently AsyncRequest::Stream does not support pausing.
}

void UpstreamRequest::insertQueueAborted() {
  insert_queue_ = nullptr;
  ENVOY_LOG(debug, "cache aborted insert operation");
  setInsertStatus(InsertStatus::InsertAbortedByCache);
  if (filter_ == nullptr) {
    abort();
  }
}

void UpstreamRequest::sendHeaders(Http::RequestHeaderMap& request_headers) {
  // If this request had a body or trailers, CacheFilter::decodeHeaders
  // would have bypassed cache lookup and insertion, so this class wouldn't
  // be instantiated. So end_stream will always be true.
  stream_->sendHeaders(request_headers, true);
}

void UpstreamRequest::abort() {
  stream_->reset(); // Calls onReset, resulting in deletion.
}

UpstreamRequest::~UpstreamRequest() {
  if (filter_ != nullptr) {
    filter_->onUpstreamRequestReset();
  }
  if (lookup_) {
    lookup_->onDestroy();
    lookup_ = nullptr;
  }
  if (insert_queue_) {
    // The insert queue may still have actions in flight, so it needs to be allowed
    // to drain itself before destruction.
    insert_queue_->setSelfOwned(std::move(insert_queue_));
  }
}

void UpstreamRequest::onReset() { delete this; }
void UpstreamRequest::onComplete() {
  if (filter_) {
    ENVOY_STREAM_LOG(debug, "UpstreamRequest complete", *filter_->decoder_callbacks_);
    filter_->onUpstreamRequestComplete();
    filter_ = nullptr;
  } else {
    ENVOY_LOG(debug, "UpstreamRequest complete after stream finished");
  }
  delete this;
}
void UpstreamRequest::disconnectFilter() {
  filter_ = nullptr;
  if (insert_queue_ == nullptr) {
    abort();
  }
}

void UpstreamRequest::onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
  if (filter_state_ == FilterState::ValidatingCachedResponse && isResponseNotModified(*headers)) {
    return processSuccessfulValidation(std::move(headers));
  }
  // Either a cache miss or a cache entry that is no longer valid.
  // Check if the new response can be cached.
  if (request_allows_inserts_ && !is_head_request_ &&
      CacheabilityUtils::isCacheableResponse(*headers, config_->varyAllowList())) {
    if (filter_) {
      ENVOY_STREAM_LOG(debug, "UpstreamRequest::onHeaders inserting headers",
                       *filter_->decoder_callbacks_);
    }
    auto insert_context =
        cache_->makeInsertContext(std::move(lookup_), *filter_->encoder_callbacks_);
    lookup_ = nullptr;
    if (insert_context != nullptr) {
      // The callbacks passed to CacheInsertQueue are all called through the dispatcher,
      // so they're thread-safe. During CacheFilter::onDestroy the queue is given ownership
      // of itself and all the callbacks are cancelled, so they are also filter-destruction-safe.
      insert_queue_ = std::make_unique<CacheInsertQueue>(cache_, *filter_->encoder_callbacks_,
                                                         std::move(insert_context), *this);
      // Add metadata associated with the cached response. Right now this is only response_time;
      const ResponseMetadata metadata = {config_->timeSource().systemTime()};
      insert_queue_->insertHeaders(*headers, metadata, end_stream);
      // insert_status_ remains absl::nullopt if end_stream == false, as we have not completed the
      // insertion yet.
      if (end_stream) {
        setInsertStatus(InsertStatus::InsertSucceeded);
      }
    }
  } else {
    setInsertStatus(InsertStatus::NoInsertResponseNotCacheable);
  }
  setFilterState(FilterState::NotServingFromCache);
  if (filter_) {
    filter_->decoder_callbacks_->encodeHeaders(std::move(headers), is_head_request_ || end_stream,
                                               StreamInfo::ResponseCodeDetails::get().ViaUpstream);
  }
}

void UpstreamRequest::onData(Buffer::Instance& body, bool end_stream) {
  if (insert_queue_ != nullptr) {
    insert_queue_->insertBody(body, end_stream);
  }
  if (filter_) {
    ENVOY_STREAM_LOG(debug, "UpstreamRequest::onData inserted body", *filter_->decoder_callbacks_);
    filter_->decoder_callbacks_->encodeData(body, end_stream);
    if (end_stream) {
      // We don't actually know at this point if the insert succeeded, but as far as the
      // filter is concerned it has been fully handed off to the cache
      // implementation.
      setInsertStatus(InsertStatus::InsertSucceeded);
    }
  } else {
    ENVOY_LOG(debug, "UpstreamRequest::onData inserted body");
  }
}

void UpstreamRequest::onTrailers(Http::ResponseTrailerMapPtr&& trailers) {
  if (insert_queue_ != nullptr) {
    insert_queue_->insertTrailers(*trailers);
  }
  if (filter_ != nullptr) {
    ENVOY_STREAM_LOG(debug, "UpstreamRequest::onTrailers inserting trailers",
                     *filter_->decoder_callbacks_);
    filter_->decoder_callbacks_->encodeTrailers(std::move(trailers));
    setInsertStatus(InsertStatus::InsertSucceeded);
  } else {
    ENVOY_LOG(debug, "UpstreamRequest::onTrailers inserting trailers");
  }
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
