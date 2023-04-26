#include "envoy/common/union_string.h"

namespace Envoy {

template <class Validator>
UnionStringBase<Validator>::UnionStringBase() : buffer_(InlinedStringVector()) {
  ASSERT((getInVec(buffer_).capacity()) >= MaxIntegerLength);
  ASSERT(valid());
}

} // namespace Envoy
