#include "common/matcher/config_dump_matcher.h"

#include "envoy/matcher/dump_matcher.h"

#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Matcher {
namespace ConfigDump {
bool isMatch(const Envoy::Protobuf::Message& dump_message, const MatchingParameters& match_params) {
  auto* factory = Envoy::Config::Utility::getFactoryByName<DumpMatcherFactory>(
      dump_message.GetDescriptor()->full_name());
  if (factory == nullptr) {
    return true;
  }
  return factory->getDumpMatcher().match(dump_message, match_params);
}

} // namespace ConfigDump
} // namespace Matcher
} // namespace Envoy