#pragma once

#include "envoy/init/init.h"
#include "envoy/server/listener_manager.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"
#include "common/http/rest_api_fetcher.h"
#include "common/json/json_validator.h"

namespace Envoy {
namespace Server {

/**
 * All LDS stats. @see stats_macros.h
 */
// clang-format off
#define ALL_LDS_STATS(COUNTER)                                                                     \
  COUNTER(update_attempt)                                                                          \
  COUNTER(update_success)                                                                          \
  COUNTER(update_failure)
// clang-format on

/**
 * Struct definition for all LDS stats. @see stats_macros.h
 */
struct LdsStats {
  ALL_LDS_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * v1 implementation of the LDS REST API.
 */
class LdsApi : Json::Validator,
               public Init::Target,
               public Http::RestApiFetcher,
               Logger::Loggable<Logger::Id::upstream> {
public:
  LdsApi(const Json::Object& config, Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
         Runtime::RandomGenerator& random, Init::Manager& init_manager,
         const LocalInfo::LocalInfo& local_info, Stats::Scope& scope, ListenerManager& lm);

  // Init::Target
  void initialize(std::function<void()> callback) override;

private:
  // Http::RestApiFetcher
  void createRequest(Http::Message& request) override;
  void parseResponse(const Http::Message& response) override;
  void onFetchComplete() override;
  void onFetchFailure(const EnvoyException* e) override;

  const LocalInfo::LocalInfo& local_info_;
  ListenerManager& lm_;
  LdsStats stats_;
  std::function<void()> initialize_callback_;
};

} // namespace Server
} // namespace Envoy
