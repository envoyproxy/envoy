#pragma once

#include "extensions/stat_sinks/circonus/circonus_stat_view.h"

#include <memory>
#include <vector>

#include "envoy/server/admin.h"
#include "envoy/server/instance.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/sink.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Circonus {

/**
 * A no-op stat-store that is only used for configuration and setup
 */
class CirconusStatSink final : public Stats::Sink {
public:
  explicit CirconusStatSink(Server::Instance& server)
      : admin_view_(std::make_unique<CirconusStatView>(server)) {}
  virtual ~CirconusStatSink() override = default;

  virtual void flush(Stats::MetricSnapshot& /* snapshot */) override{};
  virtual void onHistogramComplete(const Stats::Histogram& /* histogram */,
                                   std::uint64_t /* value */) override{};

private:
  CirconusStatViewPtr admin_view_;
};

} // namespace Circonus
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
