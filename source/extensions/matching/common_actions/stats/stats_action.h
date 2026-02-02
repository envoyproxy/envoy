#pragma once

#include "envoy/extensions/matching/common_actions/stats/v3/actions.pb.h"
#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace CommonActions {
namespace Stats {

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

} // namespace Stats
} // namespace CommonActions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
