#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "envoy/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/v3/payload_to_metadata.pb.h"

#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/extensions/filters/network/thrift_proxy/decoder.h"
#include "source/extensions/filters/network/thrift_proxy/filters/pass_through_filter.h"
#include "source/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/payload_extractor.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace PayloadToMetadataFilter {

using namespace Envoy::Extensions::NetworkFilters;
using namespace Envoy::Extensions::NetworkFilters::ThriftProxy;
using ProtoRule = envoy::extensions::filters::network::thrift_proxy::filters::payload_to_metadata::
    v3::PayloadToMetadata::Rule;
using KeyValuePair = envoy::extensions::filters::network::thrift_proxy::filters::
    payload_to_metadata::v3::PayloadToMetadata::KeyValuePair;
using ValueType = envoy::extensions::filters::network::thrift_proxy::filters::payload_to_metadata::
    v3::PayloadToMetadata::ValueType;

class Rule {
public:
  Rule(const ProtoRule& rule, uint16_t rule_id, PayloadExtractor::TrieSharedPtr root,
       Regex::Engine& regex_engine);
  const ProtoRule& rule() const { return rule_; }
  const Regex::CompiledMatcherPtr& regexRewrite() const { return regex_rewrite_; }
  const std::string& regexSubstitution() const { return regex_rewrite_substitution_; }
  uint16_t ruleId() const { return rule_id_; }
  bool matches(const ThriftProxy::MessageMetadata& metadata) const;

private:
  enum class MatchType {
    MethodName = 1,
    ServiceName = 2,
  };
  const ProtoRule rule_;
  Regex::CompiledMatcherPtr regex_rewrite_{};
  std::string regex_rewrite_substitution_{};
  std::string method_or_service_name_{};
  MatchType match_type_;
  uint16_t rule_id_;
};

using PayloadToMetadataRules = std::vector<Rule>;

class Config {
public:
  Config(const envoy::extensions::filters::network::thrift_proxy::filters::payload_to_metadata::v3::
             PayloadToMetadata& config,
         Regex::Engine& regex_engine);
  const PayloadToMetadataRules& requestRules() const { return request_rules_; }
  PayloadExtractor::TrieSharedPtr trieRoot() const { return trie_root_; };

  friend class PayloadToMetadataTest;

private:
  PayloadToMetadataRules request_rules_;
  PayloadExtractor::TrieSharedPtr trie_root_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

const uint32_t MAX_PAYLOAD_VALUE_LEN = 8 * 1024;

class PayloadToMetadataFilter : public PayloadExtractor::MetadataHandler,
                                public ThriftProxy::ThriftFilters::PassThroughDecoderFilter,
                                protected Logger::Loggable<Envoy::Logger::Id::thrift> {
public:
  PayloadToMetadataFilter(const ConfigSharedPtr config);

  // DecoderFilter
  FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
  FilterStatus passthroughData(Buffer::Instance& data) override;

  // PayloadExtractor::MetadataHandler
  // We handled messageBegin already so no need to do anything here.
  FilterStatus handleThriftMetadata(MessageMetadataSharedPtr) override {
    return FilterStatus::Continue;
  }
  void handleOnPresent(absl::variant<absl::string_view, int64_t, double> value,
                       const std::vector<uint16_t>& rule_ids, bool is_request) override;
  void handleComplete(bool is_request) override;

private:
  static ProtocolPtr createProtocol(ProtocolType protocol) {
    return NamedProtocolConfigFactory::getFactory(protocol).createProtocol();
  }

  // TODO(kuochunghsu): extract the metadata handling logic form header/payload to metadata filters.
  using StructMap = std::map<std::string, Protobuf::Struct>;
  bool addMetadata(const std::string&, const std::string&, std::string, ValueType);
  void applyKeyValue(std::string, const Rule&, const KeyValuePair&);
  const std::string& decideNamespace(const std::string& nspace) const;
  void finalizeDynamicMetadata();

  const ConfigSharedPtr config_;
  absl::flat_hash_set<uint16_t> matched_rule_ids_;
  StructMap structs_by_namespace_;
  MessageMetadataSharedPtr metadata_;
};

} // namespace PayloadToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
