#pragma once

#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/custom_response/config.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

/**
 * All stats for the custom response filter. @see stats_macros.h
 */
#define ALL_CUSTOM_RESPONSE_FILTER_STATS(COUNTER) COUNTER(get_remote_response_failed)

/**
 * Wrapper struct for stats. @see stats_macros.h
 */
struct CustomResponseFilterStats {
  ALL_CUSTOM_RESPONSE_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

class RemoteResponseClient;

class CustomResponseFilter : public Http::PassThroughFilter,
                             public Logger::Loggable<Logger::Id::filter> {
public:
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

  CustomResponseFilterStats& stats() { return stats_; }

  ~CustomResponseFilter() override = default;

  CustomResponseFilter(std::shared_ptr<FilterConfig> config,
                       Server::Configuration::FactoryContext& context,
                       const std::string& stats_prefix)
      : stats_(generateStats(stats_prefix, context.scope())), config_{std::move(config)},
        factory_context_(context) {}

  void onRemoteResponse(Http::ResponseHeaderMap& headers, const ResponseSharedPtr& custom_response,
                        const Http::ResponseMessage* response);

private:
  CustomResponseFilterStats generateStats(const std::string& stats_prefix, Stats::Scope& scope) {
    return {ALL_CUSTOM_RESPONSE_FILTER_STATS(POOL_COUNTER_PREFIX(scope, stats_prefix))};
  }

  CustomResponseFilterStats stats_;
  std::shared_ptr<FilterConfig> config_;
  Server::Configuration::FactoryContext& factory_context_;
  std::unique_ptr<RemoteResponseClient> client_;
};

// Class to fetch custom response from upstream cluster.
class RemoteResponseClient : public Http::AsyncClient::Callbacks,
                             public Logger::Loggable<Logger::Id::init> {
public:
  using RequestCB = std::function<void(const Http::ResponseMessage* response_ptr)>;
  RemoteResponseClient(const envoy::extensions::filters::http::custom_response::v3::CustomResponse::
                           Response::RemoteDataSource& config,
                       Server::Configuration::FactoryContext& context)
      : config_(config), context_(context) {}

  ~RemoteResponseClient() override { cancel(); }

  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}
  void fetchBody(RequestCB cb);
  void cancel();

  // Http::AsyncClient::Callbacks implemented by this class.
  void onSuccess(const Http::AsyncClient::Request& request,
                 Http::ResponseMessagePtr&& response) override;
  void onFailure(const Http::AsyncClient::Request& request,
                 Http::AsyncClient::FailureReason reason) override;

private:
  void onError();
  const envoy::extensions::filters::http::custom_response::v3::CustomResponse::Response::
      RemoteDataSource& config_;
  Server::Configuration::FactoryContext& context_;
  Http::AsyncClient::Request* active_request_{};
  RequestCB callback_ = nullptr;
  envoy::config::route::v3::RetryPolicy route_retry_policy_;
};

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
