#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "envoy/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/v3/payload_to_metadata.pb.h"

#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/extensions/filters/network/thrift_proxy/filters/pass_through_filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace PayloadToMetadataFilter {

using namespace Envoy::Extensions::NetworkFilters;
using ProtoRule = envoy::extensions::filters::network::thrift_proxy::filters::payload_to_metadata::
    v3::PayloadToMetadata::Rule;
using KeyValuePair = envoy::extensions::filters::network::thrift_proxy::filters::
    payload_to_metadata::v3::PayloadToMetadata::KeyValuePair;
using ValueType = envoy::extensions::filters::network::thrift_proxy::filters::payload_to_metadata::
    v3::PayloadToMetadata::ValueType;

class Rule {
public:
  Rule(const ProtoRule& rule);
  const ProtoRule& rule() const { return rule_; }
  const Regex::CompiledMatcherPtr& regexRewrite() const { return regex_rewrite_; }
  const std::string& regexSubstitution() const { return regex_rewrite_substitution_; }
  std::shared_ptr<const HeaderValueSelector> selector_;

private:
  const ProtoRule rule_;
  Regex::CompiledMatcherPtr regex_rewrite_{};
  std::string regex_rewrite_substitution_{};
};

using PayloadToMetadataRules = std::vector<Rule>;

class Config {
public:
  Config(const envoy::extensions::filters::network::thrift_proxy::filters::payload_to_metadata::v3::
             PayloadToMetadata& config);
  // const PayloadToMetadataRules& requestRules() const { return request_rules_; }

private:
  // PayloadToMetadataRules request_rules_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;


class RuleMatch {
public:
  bool matched_{false};
  bool handled_{false};
  // const Rule& rule_;
};

using RuleMatches = std::vector<RuleMatch>;

class PayloadToMetadataFilter : public ThriftProxy::ThriftFilters::PassThroughDecoderFilter,
                               protected Logger::Loggable<Envoy::Logger::Id::main> {
public:
  PayloadToMetadataFilter(const ConfigSharedPtr config);

  bool passthroughSupported() const override;
private:
  const ConfigSharedPtr config_;
  RuleMatches matches_;
};

} // namespace PayloadToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
