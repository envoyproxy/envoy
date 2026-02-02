#pragma once

#include "envoy/extensions/matching/actions/stats/v3/stats.pb.h"

#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace Stat {

struct ActionContext {};

class StatsAction {
public:
  /**
   * The result of the action application.
   */
  enum class Result {
    // The stat should be kept (emitted).
    Keep,
    // The stat should be dropped (not emitted).
    Drop,
  };

  virtual ~StatsAction() = default;

  /**
   * Applies the action to the given tags.
   * @param tags supplied the tags to be applied.
   * @return Result result of the action.
   */
  virtual Result apply(Envoy::Stats::TagVector& tags) const PURE;
};

} // namespace Stat
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
