#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

#include "envoy/config/filter/http/health_check/v2/health_check.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class HealthCheckFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object& json_config, const std::string&,
                                          FactoryContext& context) override;
  HttpFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                   const std::string& stats_prefix,
                                                   FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::config::filter::http::health_check::v2::HealthCheck()};
  }

  std::string name() override { return Config::HttpFilterNames::get().HEALTH_CHECK; }

private:
  HttpFilterFactoryCb
  createFilter(const envoy::config::filter::http::health_check::v2::HealthCheck& proto_config,
               const std::string& stats_prefix, FactoryContext& context);
};

} // namespace Configuration
} // namespace Server

/**
 * Shared cache manager used by all instances of a health check filter configuration as well as
 * all threads. This sets up a timer that will invalidate the cached response code and allow some
 * requests to go through to the backend. No attempt is made to allow only a single request to go
 * through to the backend, so during the invalidation window some number of requests will get
 * through.
 */
class HealthCheckCacheManager {
public:
  HealthCheckCacheManager(Event::Dispatcher& dispatcher, std::chrono::milliseconds timeout);

  Http::Code getCachedResponseCode() { return last_response_code_; }
  void setCachedResponseCode(Http::Code code) {
    last_response_code_ = code;
    use_cached_response_code_ = true;
  }
  bool useCachedResponseCode() { return use_cached_response_code_; }

private:
  void onTimer();

  Event::TimerPtr clear_cache_timer_;
  const std::chrono::milliseconds timeout_;
  std::atomic<bool> use_cached_response_code_{};
  std::atomic<Http::Code> last_response_code_{};
};

typedef std::shared_ptr<HealthCheckCacheManager> HealthCheckCacheManagerSharedPtr;

typedef std::map<std::string, double> ClusterMinHealthyPercentages;
typedef std::shared_ptr<const ClusterMinHealthyPercentages>
    ClusterMinHealthyPercentagesConstSharedPtr;

/**
 * Health check responder filter.
 */
class HealthCheckFilter : public Http::StreamFilter {
public:
  HealthCheckFilter(Server::Configuration::FactoryContext& context, bool pass_through_mode,
                    HealthCheckCacheManagerSharedPtr cache_manager, const std::string& endpoint,
                    ClusterMinHealthyPercentagesConstSharedPtr cluster_min_healthy_percentages)
      : context_(context), pass_through_mode_(pass_through_mode), cache_manager_(cache_manager),
        endpoint_(endpoint), cluster_min_healthy_percentages_(cluster_min_healthy_percentages) {}

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks&) override {}

private:
  void onComplete();

  Server::Configuration::FactoryContext& context_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  bool handling_{};
  bool health_check_request_{};
  bool pass_through_mode_{};
  HealthCheckCacheManagerSharedPtr cache_manager_;
  const std::string endpoint_;
  ClusterMinHealthyPercentagesConstSharedPtr cluster_min_healthy_percentages_;
};
} // namespace Envoy
