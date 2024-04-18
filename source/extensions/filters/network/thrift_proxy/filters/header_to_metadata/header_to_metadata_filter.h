#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "envoy/extensions/filters/network/thrift_proxy/filters/header_to_metadata/v3/header_to_metadata.pb.h"

#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/extensions/filters/network/thrift_proxy/filters/pass_through_filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace HeaderToMetadataFilter {

using namespace Envoy::Extensions::NetworkFilters;
using ProtoRule = envoy::extensions::filters::network::thrift_proxy::filters::header_to_metadata::
    v3::HeaderToMetadata::Rule;
using KeyValuePair = envoy::extensions::filters::network::thrift_proxy::filters::
    header_to_metadata::v3::HeaderToMetadata::KeyValuePair;
using ValueType = envoy::extensions::filters::network::thrift_proxy::filters::header_to_metadata::
    v3::HeaderToMetadata::ValueType;
using ValueEncode = envoy::extensions::filters::network::thrift_proxy::filters::header_to_metadata::
    v3::HeaderToMetadata::ValueEncode;

// Get value from a header.
class HeaderValueSelector {
public:
  explicit HeaderValueSelector(Http::LowerCaseString header, bool remove)
      : header_(std::move(header)), remove_(std::move(remove)) {}
  absl::optional<std::string> extract(Http::HeaderMap& map) const;
  std::string toString() const { return fmt::format("header '{}'", header_.get()); }

private:
  const Http::LowerCaseString header_;
  const bool remove_;
};

class Rule {
public:
  Rule(const ProtoRule& rule, Regex::Engine& regex_engine);
  const ProtoRule& rule() const { return rule_; }
  const Regex::CompiledMatcherPtr& regexRewrite() const { return regex_rewrite_; }
  const std::string& regexSubstitution() const { return regex_rewrite_substitution_; }
  std::shared_ptr<const HeaderValueSelector> selector_;

private:
  const ProtoRule rule_;
  Regex::CompiledMatcherPtr regex_rewrite_{};
  std::string regex_rewrite_substitution_{};
};

using HeaderToMetadataRules = std::vector<Rule>;

const uint32_t MAX_HEADER_VALUE_LEN = 8 * 1024;

class Config {
public:
  Config(const envoy::extensions::filters::network::thrift_proxy::filters::header_to_metadata::v3::
             HeaderToMetadata& config,
         Regex::Engine& regex_engine);
  const HeaderToMetadataRules& requestRules() const { return request_rules_; }

private:
  HeaderToMetadataRules request_rules_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

class HeaderToMetadataFilter : public ThriftProxy::ThriftFilters::PassThroughDecoderFilter,
                               protected Logger::Loggable<Envoy::Logger::Id::main> {
public:
  HeaderToMetadataFilter(const ConfigSharedPtr config);

  ThriftProxy::FilterStatus
      transportBegin(Extensions::NetworkFilters::ThriftProxy::MessageMetadataSharedPtr) override;

private:
  using ProtobufRepeatedRule = Protobuf::RepeatedPtrField<ProtoRule>;
  using StructMap = std::map<std::string, ProtobufWkt::Struct>;

  /**
   *  writeHeaderToMetadata encapsulates (1) searching for the header and (2) writing it to the
   *  request metadata.
   *  @param headers the map of key-value headers to look through. These are request headers.
   *  @param rules the header-to-metadata mapping set in configuration.
   */
  void writeHeaderToMetadata(Http::HeaderMap& headers, const HeaderToMetadataRules& rules,
                             ThriftProxy::ThriftFilters::DecoderFilterCallbacks& callbacks) const;
  bool addMetadata(StructMap&, const std::string&, const std::string&, std::string, ValueType,
                   ValueEncode) const;
  void applyKeyValue(std::string&&, const Rule&, const KeyValuePair&, StructMap&) const;
  const std::string& decideNamespace(const std::string& nspace) const;

  const ConfigSharedPtr config_;
};

} // namespace HeaderToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
