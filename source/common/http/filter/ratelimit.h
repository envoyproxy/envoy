#pragma once

#include "envoy/http/filter.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/runtime/runtime.h"

#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"

namespace Http {
namespace RateLimit {

class FilterConfig;

/**
 * Generic rate limit action that the filter performs.
 */
class Action {
public:
  virtual ~Action() {}

  /**
   * Potentially populate the descriptor array with new descriptors to query.
   * @param route supplies the target route for the request.
   * @param descriptors supplies the descriptor array to optionally fill.
   * @param config supplies the filter configuration.
   */
  virtual void populateDescriptors(const Router::RouteEntry& route,
                                   std::vector<::RateLimit::Descriptor>& descriptors,
                                   FilterConfig& config, const HeaderMap& headers,
                                   StreamDecoderFilterCallbacks& callbacks) PURE;
};

typedef std::unique_ptr<Action> ActionPtr;

/**
 * Action for service to service rate limiting.
 */
class ServiceToServiceAction : public Action {
public:
  // Action
  void populateDescriptors(const Router::RouteEntry& route,
                           std::vector<::RateLimit::Descriptor>& descriptors, FilterConfig& config,
                           const HeaderMap&, StreamDecoderFilterCallbacks&) override;
};

/**
 * Action for request headers rate limiting.
 */
class RequestHeadersAction : public Action {
public:
  RequestHeadersAction(const Json::Object& action)
      : header_name_(action.getString("header_name")),
        descriptor_key_(action.getString("descriptor_key")) {}
  // Action
  void populateDescriptors(const Router::RouteEntry& route,
                           std::vector<::RateLimit::Descriptor>& descriptors, FilterConfig& config,
                           const HeaderMap& headers, StreamDecoderFilterCallbacks&) override;

private:
  const LowerCaseString header_name_;
  const std::string descriptor_key_;
};

/**
 * Action for remote address rate limiting.
 */
class RemoteAddressAction : public Action {
public:
  // Action
  void populateDescriptors(const Router::RouteEntry& route,
                           std::vector<::RateLimit::Descriptor>& descriptors, FilterConfig&,
                           const HeaderMap&, StreamDecoderFilterCallbacks& callbacks) override;
};

/**
 * Global configuration for the HTTP rate limit filter.
 */
class FilterConfig {
public:
  FilterConfig(const Json::Object& config, const std::string& local_service_cluster,
               Stats::Store& stats_store, Runtime::Loader& runtime);

  const std::vector<ActionPtr>& actions() { return actions_; }
  const std::string& domain() { return domain_; }
  const std::string& localServiceCluster() { return local_service_cluster_; }
  Runtime::Loader& runtime() { return runtime_; }
  Stats::Store& stats() { return stats_store_; }

private:
  const std::string domain_;
  const std::string local_service_cluster_;
  Stats::Store& stats_store_;
  Runtime::Loader& runtime_;
  std::vector<ActionPtr> actions_;
};

typedef std::shared_ptr<FilterConfig> FilterConfigPtr;

/**
 * HTTP rate limit filter. Depending on the route configuration, this filter calls the global
 * rate limiting service before allowing further filter iteration.
 */
class Filter : public StreamDecoderFilter, public ::RateLimit::RequestCallbacks {
public:
  Filter(FilterConfigPtr config, ::RateLimit::ClientPtr&& client)
      : config_(config), client_(std::move(client)) {}

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus decodeTrailers(HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

  // RateLimit::RequestCallbacks
  void complete(::RateLimit::LimitStatus status) override;

private:
  enum class State { NotStarted, Calling, Complete, Responded };

  static const Http::HeaderMapPtr TOO_MANY_REQUESTS_HEADER;

  FilterConfigPtr config_;
  ::RateLimit::ClientPtr client_;
  StreamDecoderFilterCallbacks* callbacks_{};
  bool initiating_call_{};
  State state_{State::NotStarted};
  std::string cluster_ratelimit_stat_prefix_;
  std::string cluster_stat_prefix_;
};

} // RateLimit
} // Http
