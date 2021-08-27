#pragma once

#include "source/common/stats/utility.h"

namespace Envoy {
namespace Stats {

// These two trivial functions are broken out into a separate compilation unit
// to make sure the optimizer cannot hoist vector-creation out of the loop. They
// simply create vectors based on their 5 inputs.
ElementVec makeElements(Element a, Element b, Element c, Element d, Element e);
StatNameVec makeStatNames(StatName a, StatName b, StatName c, StatName d, StatName e);

} // namespace Stats
} // namespace Envoy
