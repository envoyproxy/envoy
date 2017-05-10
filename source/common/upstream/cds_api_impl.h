#pragma once

#include <functional>

#include "envoy/event/dispatcher.h"
#include "envoy/json/json_object.h"
#include "envoy/local_info/local_info.h"

#include "common/common/logger.h"
#include "common/http/rest_api_fetcher.h"

namespace Lyft {
namespace Upstream {

/**
 * All CDS stats. @see stats_macros.h
 */
// clang-format off
#define ALL_CDS_STATS(COUNTER)                                                                     \
  COUNTER(update_attempt)                                                                          \
  COUNTER(update_success)                                                                          \
  COUNTER(update_failure)
// clang-format on

/**
 * Struct definition for all CDS stats. @see stats_macros.h
 */
struct CdsStats {
  ALL_CDS_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * REST fetching implementation of the CDS API.
 */
class CdsApiImpl : public CdsApi, Http::RestApiFetcher, Logger::Loggable<Logger::Id::upstream> {
public:
  static CdsApiPtr create(const Json::Object& config, ClusterManager& cm,
                          Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                          const LocalInfo::LocalInfo& local_info, Stats::Scope& scope);

  // Upstream::CdsApi
  void initialize() override { RestApiFetcher::initialize(); }
  void setInitializedCb(std::function<void()> callback) override {
    initialize_callback_ = callback;
  }

private:
  CdsApiImpl(const Json::Object& config, ClusterManager& cm, Event::Dispatcher& dispatcher,
             Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
             Stats::Scope& scope);

  // Http::RestApiFetcher
  void createRequest(Http::Message& request) override;
  void parseResponse(const Http::Message& response) override;
  void onFetchComplete() override;
  void onFetchFailure(EnvoyException* e) override;

  const LocalInfo::LocalInfo& local_info_;
  CdsStats stats_;
  std::function<void()> initialize_callback_;
};

} // Upstream
} // Lyft