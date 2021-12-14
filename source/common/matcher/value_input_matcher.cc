#include "source/common/matcher/value_input_matcher.h"

#include "envoy/type/matcher/v3/string.pb.h"

#include "xds/type/matcher/v3/string.pb.h"

namespace Envoy {
namespace Matcher {

template <>
StringInputMatcher<envoy::type::matcher::v3::StringMatcher>::StringInputMatcher(
    const envoy::type::matcher::v3::StringMatcher& matcher, ProtobufMessage::ValidationVisitor&)
    : matcher_(matcher) {}

template <>
StringInputMatcher<xds::type::matcher::v3::StringMatcher>::StringInputMatcher(
    const xds::type::matcher::v3::StringMatcher& matcher,
    ProtobufMessage::ValidationVisitor& validation_visitor)
    : matcher_(matcher, validation_visitor) {}

} // namespace Matcher
} // namespace Envoy
