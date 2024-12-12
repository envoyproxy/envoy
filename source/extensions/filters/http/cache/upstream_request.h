#pragma once

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/cache/cache_filter_logging_info.h"
#include "source/extensions/filters/http/cache/cache_insert_queue.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class CacheFilter;
class CacheFilterConfig;
enum class FilterState;

class UpstreamRequest : public Logger::Loggable<Logger::Id::cache_filter>,
                        public Http::AsyncClient::StreamCallbacks,
                        public InsertQueueCallbacks {
public:
  void sendHeaders(Http::RequestHeaderMap& request_headers);
  // Called by filter_ when filter_ is destroyed first.
  // UpstreamRequest will make no more calls to filter_ once disconnectFilter
  // has been called.
  void disconnectFilter();

  // StreamCallbacks
  void onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void onData(Buffer::Instance& data, bool end_stream) override;
  void onTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
  void onComplete() override;
  void onReset() override;

  // InsertQueueCallbacks
  void insertQueueOverHighWatermark() override;
  void insertQueueUnderLowWatermark() override;
  void insertQueueAborted() override;

  static UpstreamRequest* create(CacheFilter* filter, LookupContextPtr lookup,
                                 LookupResultPtr lookup_result, std::shared_ptr<HttpCache> cache,
                                 Http::AsyncClient& async_client,
                                 const Http::AsyncClient::StreamOptions& options);
  UpstreamRequest(CacheFilter* filter, LookupContextPtr lookup, LookupResultPtr lookup_result,
                  std::shared_ptr<HttpCache> cache, Http::AsyncClient& async_client,
                  const Http::AsyncClient::StreamOptions& options);
  ~UpstreamRequest() override;

private:
  // Precondition: lookup_result_ points to a cache lookup result that requires validation.
  //               filter_state_ is ValidatingCachedResponse.
  // Serves a validated cached response after updating it with a 304 response.
  void processSuccessfulValidation(Http::ResponseHeaderMapPtr response_headers);

  // Updates the filter state belonging to the UpstreamRequest, and the one belonging to
  // the filter if it has not been destroyed.
  void setFilterState(FilterState fs);

  // Updates the insert status belonging to the filter, if it has not been destroyed.
  void setInsertStatus(InsertStatus is);

  // If an error occurs while the stream is active, abort will reset the stream, which
  // in turn provokes the rest of the destruction process.
  void abort();

  // Precondition: lookup_result_ points to a cache lookup result that requires validation.
  //               filter_state_ is ValidatingCachedResponse.
  // Checks if a cached entry should be updated with a 304 response.
  bool shouldUpdateCachedEntry(const Http::ResponseHeaderMap& response_headers) const;

  CacheFilter* filter_ = nullptr;
  LookupContextPtr lookup_;
  LookupResultPtr lookup_result_;
  bool is_head_request_;
  bool request_allows_inserts_;
  std::shared_ptr<const CacheFilterConfig> config_;
  FilterState filter_state_;
  std::shared_ptr<HttpCache> cache_;
  Http::AsyncClient::Stream* stream_ = nullptr;
  std::unique_ptr<CacheInsertQueue> insert_queue_;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
