#pragma once

#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/listener/listener.pb.h"
#include "envoy/json/json_object.h"
#include "envoy/stats/stats_options.h"

namespace Envoy {
namespace Config {

class LdsJson {
public:
  /**
   * Translate a v1 JSON Listener to v2 envoy::api::v2::Listener.
   * @param json_listener source v1 JSON Listener object.
   * @param listener destination v2 envoy::api::v2::Listener.
   */
  static void translateListener(const Json::Object& json_listener,
                                envoy::api::v2::Listener& listener,
                                const Stats::StatsOptions& stats_options);
};

} // namespace Config
} // namespace Envoy
