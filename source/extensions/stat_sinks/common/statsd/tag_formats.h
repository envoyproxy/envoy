#pragma once

#include <string>

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Common {
namespace Statsd {

enum class TagPosition { TagAfterValue, TagAfterName };

struct TagFormat {
  const std::string start;
  const std::string assign;
  const std::string separator;
  const TagPosition tag_position;
};

const TagFormat& getDefaultTagFormat();
const TagFormat& getGraphiteTagFormat();

} // namespace Statsd
} // namespace Common
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
