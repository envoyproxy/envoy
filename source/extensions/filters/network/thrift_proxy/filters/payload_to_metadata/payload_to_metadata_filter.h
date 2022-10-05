#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "envoy/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/v3/payload_to_metadata.pb.h"

#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/extensions/filters/network/thrift_proxy/decoder.h"
#include "source/extensions/filters/network/thrift_proxy/filters/pass_through_filter.h"

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
  Rule(const ProtoRule& rule, uint16_t id);
  const ProtoRule& rule() const { return rule_; }
  const Regex::CompiledMatcherPtr& regexRewrite() const { return regex_rewrite_; }
  const std::string& regexSubstitution() const { return regex_rewrite_substitution_; }
  uint16_t id() const { return id_; }
  bool matches(const ThriftProxy::MessageMetadata& metadata) const;

private:
  enum class MatchType {
    METHOD_NAME = 1,
    SERVICE_NAME = 2,
  };
  const ProtoRule rule_;
  Regex::CompiledMatcherPtr regex_rewrite_{};
  std::string regex_rewrite_substitution_{};
  std::string method_or_service_name_{};
  MatchType match_type_;
  uint16_t id_;
};

using PayloadToMetadataRules = std::vector<Rule>;

struct Trie;
using TrieSharedPtr = std::shared_ptr<Trie>;

struct Trie {
  TrieSharedPtr parent_;
  absl::node_hash_map<uint16_t, TrieSharedPtr> children_;
  OptRef<Rule> rule_;
};

class Config {
public:
  Config(const envoy::extensions::filters::network::thrift_proxy::filters::payload_to_metadata::v3::
             PayloadToMetadata& config);
  const PayloadToMetadataRules& requestRules() const { return request_rules_; }
  TrieSharedPtr trieRoot() const { return trie_root_; };

private:
  PayloadToMetadataRules request_rules_;
  TrieSharedPtr trie_root_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

class TrieMatchHandler : public DecoderCallbacks,
                         public PassThroughDecoderEventHandler,
                         protected Logger::Loggable<Envoy::Logger::Id::main> {
public:
  TrieMatchHandler(TrieSharedPtr root) : node_(root) {}

  // DecoderEventHandler
  FilterStatus messageEnd() override;
  FilterStatus structBegin(absl::string_view) override;
  FilterStatus structEnd() override;
  FilterStatus fieldBegin(absl::string_view name, FieldType& field_type,
                          int16_t& field_id) override;
  FilterStatus fieldEnd() override;
  FilterStatus byteValue(uint8_t& value) override { return handleNumber(value); }
  FilterStatus int16Value(int16_t& value) override { return handleNumber(value); }
  FilterStatus int32Value(int32_t& value) override { return handleNumber(value); }
  FilterStatus int64Value(int64_t& value) override { return handleNumber(value); }
  FilterStatus doubleValue(double& value) override { return handleNumber(value); }
  FilterStatus stringValue(absl::string_view value) override { return handleString(value); }

  // DecoderCallbacks
  DecoderEventHandler& newDecoderEventHandler() override { return *this; }
  bool passthroughEnabled() const override { return true; }
  bool isRequest() const override { return false; }
  bool headerKeysPreserveCase() const override { return false; }

  bool isComplete() const { return complete_; };

private:
  template <typename NumberType> FilterStatus handleNumber(NumberType value);

  FilterStatus handleString(absl::string_view value);
  TrieSharedPtr node_;
  bool complete_{false};
};

using TrieMatchHandlerPtr = std::unique_ptr<TrieMatchHandler>;

class PayloadToMetadataFilter : public ThriftProxy::ThriftFilters::PassThroughDecoderFilter,
                                protected Logger::Loggable<Envoy::Logger::Id::main> {
public:
  PayloadToMetadataFilter(const ConfigSharedPtr config);

  // DecoderFilter
  FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
  FilterStatus passthroughData(Buffer::Instance& data) override;

private:
  static TransportPtr createTransport(TransportType transport) {
    return NamedTransportConfigFactory::getFactory(transport).createTransport();
  }

  static ProtocolPtr createProtocol(ProtocolType protocol) {
    return NamedProtocolConfigFactory::getFactory(protocol).createProtocol();
  }

  const ConfigSharedPtr config_;
  absl::flat_hash_set<uint16_t> matched_ids_;
  TrieMatchHandlerPtr handler_;
  TransportPtr transport_;
  ProtocolPtr protocol_;
  DecoderPtr decoder_;
};

} // namespace PayloadToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
