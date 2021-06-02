#pragma once

#include "envoy/matcher/dump_matcher.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Matcher {
namespace ConfigDump {
bool isMatch(const Envoy::Protobuf::Message& dump_message, const MatchingParameters& match_params);
} // namespace ConfigDump
} // namespace Matcher
} // namespace Envoy
