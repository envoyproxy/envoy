#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"

#include "common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Matcher {
namespace ConfigDump {
using MatchingParameters = absl::flat_hash_map<std::string, std::string>;

/**
 * Implement this class and its `match` method for all the types that should
 * have config dump filtering enabled. For example:
 * class RouteConfigDumpMatcher : DumpMatcher {
 *   bool match(const proto2::Message& dump_message) {
 *     // cast dump_message to the correct proto, and perform the match
 *   }
 * };
 */
class DumpMatcher {
public:
  virtual ~DumpMatcher() = default;

  virtual bool match(const Protobuf::Message& dump_message,
                     const MatchingParameters& matchers) const PURE;
};

// Must have `name` method which returns the fully qualified proto name that the
// matcher is supposed to match.
class DumpMatcherFactory : public Config::UntypedFactory {
public:
  virtual ~DumpMatcherFactory() = default;

  virtual const DumpMatcher& getDumpMatcher() PURE;

  std::string category() const override { return "envoy.matching.config_dump"; }
};

} // namespace ConfigDump
} // namespace Matcher
} // namespace Envoy
