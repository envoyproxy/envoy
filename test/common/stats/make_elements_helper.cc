#include "common/stats/utility.h"

namespace Envoy {
namespace Stats {

ElementVec makeElements(Element a, Element b, Element c, Element d, Element e) {
  return ElementVec{a, b, c, d, e};
}

StatNameVec makeStatNames(StatName a, StatName b, StatName c, StatName d, StatName e) {
  return StatNameVec{a, b, c, d, e};
}

} // namespace Stats
} // namespace Envoy
