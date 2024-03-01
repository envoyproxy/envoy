#include "source/common/config/stats_utility.h"

#include "source/common/stats/histogram_impl.h"
#include "source/common/stats/stats_matcher_impl.h"
#include "source/common/stats/tag_producer_impl.h"

namespace Envoy {
namespace Config {

Stats::TagProducerPtr
StatsUtility::createTagProducer(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                                const Stats::TagVector& cli_tags) {
  return std::make_unique<Stats::TagProducerImpl>(bootstrap.stats_config(), cli_tags);
}

} // namespace Config
} // namespace Envoy
