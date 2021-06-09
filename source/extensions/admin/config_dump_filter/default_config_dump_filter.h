#pragma once

#include <string>

#include "envoy/extensions/admin/config_dump_filter/v3alpha/config_dump_filter.pb.h"
#include "envoy/server/config_dump_config.h"

#include "source/common/common/regex.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace ConfigDumpFilters {

constexpr absl::string_view kDefaultName = "envoy.admin.config_dump_filter.default";

class DefaultConfigDumpFilter : public Server::Configuration::ConfigDumpFilter {
public:
  explicit DefaultConfigDumpFilter(const Server::Configuration::MatchingParameters& params);
  bool match(const Protobuf::Message& message) const override;

private:
  const Regex::CompiledMatcherPtr name_regex_;
};

class DefaultConfigDumpFilterFactory : public Server::Configuration::ConfigDumpFilterFactory {
public:
  Server::Configuration::ConfigDumpFilterPtr
  createConfigDumpFilter(const Server::Configuration::MatchingParameters& params) const override;
  std::string name() const override { return std::string(kDefaultName); }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::unique_ptr<
        envoy::extensions::admin::config_dump_filter::v3alpha::DefaultConfigDumpFilter>();
  }
};
} // namespace ConfigDumpFilters
} // namespace Extensions
} // namespace Envoy