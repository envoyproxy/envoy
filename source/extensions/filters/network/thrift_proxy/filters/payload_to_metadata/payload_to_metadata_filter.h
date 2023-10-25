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

struct Trie;
using TrieSharedPtr = std::shared_ptr<Trie>;

class Rule {
public:
  Rule(const ProtoRule& rule, uint16_t rule_id, TrieSharedPtr root);
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

struct Trie {
  Trie(TrieSharedPtr parent = nullptr) : parent_(parent) {}
  std::string name_;
  std::weak_ptr<Trie> parent_;
  // Field ID to payload node
  absl::node_hash_map<int16_t, TrieSharedPtr> children_;
  std::vector<uint16_t> rule_ids_;
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

class MetadataHandler {
public:
  virtual ~MetadataHandler() = default;
  virtual void handleOnPresent(std::string&& value, const std::vector<uint16_t>& rule_ids) PURE;
  virtual void handleOnMissing() PURE;
};

class TrieMatchHandler : public DecoderCallbacks,
                         public PassThroughDecoderEventHandler,
                         protected Logger::Loggable<Envoy::Logger::Id::thrift> {
public:
  TrieMatchHandler(MetadataHandler& parent, TrieSharedPtr root) : parent_(parent), node_(root) {}

  // DecoderEventHandler
  FilterStatus messageEnd() override;
  FilterStatus structBegin(absl::string_view) override;
  FilterStatus structEnd() override;
  FilterStatus fieldBegin(absl::string_view name, FieldType& field_type,
                          int16_t& field_id) override;
  FilterStatus fieldEnd() override;
  FilterStatus boolValue(bool& value) override { return numberValue(value); }
  FilterStatus byteValue(uint8_t& value) override { return numberValue(value); }
  FilterStatus int16Value(int16_t& value) override { return numberValue(value); }
  FilterStatus int32Value(int32_t& value) override { return numberValue(value); }
  FilterStatus int64Value(int64_t& value) override { return numberValue(value); }
  FilterStatus doubleValue(double& value) override { return numberValue(value); }
  FilterStatus stringValue(absl::string_view value) override;
  FilterStatus mapBegin(FieldType&, FieldType&, uint32_t&) override {
    return handleContainerBegin();
  }
  FilterStatus mapEnd() override { return handleContainerEnd(); }
  FilterStatus listBegin(FieldType&, uint32_t&) override { return handleContainerBegin(); }
  FilterStatus listEnd() override { return handleContainerEnd(); }
  FilterStatus setBegin(FieldType&, uint32_t&) override { return handleContainerBegin(); }
  FilterStatus setEnd() override { return handleContainerEnd(); }

  // DecoderCallbacks
  DecoderEventHandler& newDecoderEventHandler() override { return *this; }
  bool passthroughEnabled() const override { return false; }
  bool isRequest() const override { return true; }
  bool headerKeysPreserveCase() const override { return false; }

  bool isComplete() const { return complete_; };

private:
  template <typename NumberType> FilterStatus numberValue(NumberType value);
  FilterStatus handleString(std::string value);

  FilterStatus handleContainerBegin() {
    steps_++;
    return FilterStatus::Continue;
  }

  FilterStatus handleContainerEnd() {
    steps_--;
    return FilterStatus::Continue;
  }

  MetadataHandler& parent_;
  TrieSharedPtr node_;
  bool complete_{false};
  absl::optional<int16_t> last_field_id_;
  uint16_t steps_{0};
};

const uint32_t MAX_PAYLOAD_VALUE_LEN = 8 * 1024;

class PayloadToMetadataFilter : public MetadataHandler,
                                public ThriftProxy::ThriftFilters::PassThroughDecoderFilter,
                                protected Logger::Loggable<Envoy::Logger::Id::thrift> {
public:
  PayloadToMetadataFilter(const ConfigSharedPtr config);

  // DecoderFilter
  FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
  FilterStatus passthroughData(Buffer::Instance& data) override;

  // MetadataHandler
  void handleOnPresent(std::string&& value, const std::vector<uint16_t>& rule_ids) override;
  void handleOnMissing() override;

private:
  static ProtocolPtr createProtocol(ProtocolType protocol) {
    return NamedProtocolConfigFactory::getFactory(protocol).createProtocol();
  }

  // TODO(kuochunghsu): extract the metadata handling logic form header/payload to metadata filters.
  using StructMap = std::map<std::string, ProtobufWkt::Struct>;
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
