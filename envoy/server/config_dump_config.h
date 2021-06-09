#pragma once

#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class ConfigDumpFilter {
public:
  virtual ~ConfigDumpFilter() = default;

  /**
   * @param message message that will be included in ConfigDump if method returns true.
   */
  virtual bool match(const Protobuf::Message& message) const PURE;
};

using ConfigDumpFilterPtr = std::unique_ptr<ConfigDumpFilter>;
using MatchingParameters = absl::flat_hash_map<std::string, std::string>;

/**
 * Implemented for generating custom filtering options for ConfigDump.
 */
class ConfigDumpFilterFactory : public Config::TypedFactory {
public:
  ~ConfigDumpFilterFactory() override = default;

  virtual ConfigDumpFilterPtr createConfigDumpFilter(const MatchingParameters& params) const;

  std::string category() const override { return "envoy.admin.config_dump_filter"; }
};
} // namespace Configuration
} // namespace Server
} // namespace Envoy