#include "source/extensions/stat_sinks/common/statsd/tag_formats.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Common {
namespace Statsd {

const TagFormat& getDefaultTagFormat() {
  CONSTRUCT_ON_FIRST_USE(TagFormat, TagFormat{
                                        "|#",                       // start
                                        ":",                        // assign
                                        ",",                        // separator
                                        TagPosition::TagAfterValue, // tag_position
                                    });
}

const TagFormat& getGraphiteTagFormat() {
  CONSTRUCT_ON_FIRST_USE(TagFormat, TagFormat{
                                        ";",                       // start
                                        "=",                       // assign
                                        ";",                       // separator
                                        TagPosition::TagAfterName, // tag_position
                                    });
}

} // namespace Statsd
} // namespace Common
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
