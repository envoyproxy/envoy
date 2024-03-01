#pragma once

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stats/stats_matcher.h"
#include "envoy/stats/tag_producer.h"

namespace Envoy {
namespace Config {

class StatsUtility {
public:
  /**
   * Create TagProducer instance. Check all tag names for conflicts to avoid
   * unexpected tag name overwriting.
   * @param bootstrap bootstrap proto.
   * @param cli_tags tags that are provided by the cli
   * @throws EnvoyException when the conflict of tag names is found.
   */
  static Stats::TagProducerPtr
  createTagProducer(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                    const Stats::TagVector& cli_tags);
};

} // namespace Config
} // namespace Envoy
