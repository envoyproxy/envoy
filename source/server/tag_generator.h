#pragma once

#include <cstdint>
#include <set>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Server {

/**
 * Generate tag for filter chain. The tag is used to identify the connections belong to the tag
 * asociated filter chain. If two filter chains are different, their tags must be different. Two
 * identical filter chains should shared the same tag but it is not required.
 */
class TagGenerator {
public:
  using Tags = std::set<uint64_t>;
  virtual ~TagGenerator() = default;
  virtual Tags getTags() PURE;
};
} // namespace Server
} // namespace Envoy