#pragma once

#include <string>

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Common {
namespace Statsd {

enum class TagPosition { TagAfterValue, TagAfterName };

struct TagFormat {
  const std::string& start;
  const std::string& assign;
  const std::string& separator;
  const TagPosition tag_position;
};

// TODO: construct on first use?
const TagFormat DefaultTagFormat = {
    "|#",                       // start
    ":",                        // assign
    ",",                        // separator
    TagPosition::TagAfterValue, // tag_position
};

// TODO: construct on first use?
const TagFormat GraphiteTagFormat = {
    ";",                       // start
    "=",                       // assign
    ";",                       // separator
    TagPosition::TagAfterName, // tag_position
};

} // namespace Statsd
} // namespace Common
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
