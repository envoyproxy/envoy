#include "source/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/payload_to_metadata_filter.h"

#include "source/common/common/regex.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/network/thrift_proxy/auto_protocol_impl.h"
#include "source/extensions/filters/network/thrift_proxy/auto_transport_impl.h"
#include "source/extensions/filters/network/thrift_proxy/binary_protocol_impl.h"
#include "source/extensions/filters/network/thrift_proxy/compact_protocol_impl.h"
#include "source/extensions/filters/network/thrift_proxy/framed_transport_impl.h"
#include "source/extensions/filters/network/thrift_proxy/header_transport_impl.h"
#include "source/extensions/filters/network/thrift_proxy/unframed_transport_impl.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace PayloadToMetadataFilter {

using namespace Envoy::Extensions::NetworkFilters;
using FieldSelector = envoy::extensions::filters::network::thrift_proxy::filters::
    payload_to_metadata::v3::PayloadToMetadata::FieldSelector;

Config::Config(const envoy::extensions::filters::network::thrift_proxy::filters::
                   payload_to_metadata::v3::PayloadToMetadata& config,
               Regex::Engine& regex_engine) {
  trie_root_ = std::make_shared<Trie>();
  request_rules_.reserve(config.request_rules().size());
  for (const auto& entry : config.request_rules()) {
    request_rules_.emplace_back(entry, static_cast<uint16_t>(request_rules_.size()), trie_root_,
                                regex_engine);
  }
}

Rule::Rule(const ProtoRule& rule, uint16_t rule_id, TrieSharedPtr root, Regex::Engine& regex_engine)
    : rule_(rule), rule_id_(rule_id) {
  if (!rule_.has_on_present() && !rule_.has_on_missing()) {
    throw EnvoyException("payload to metadata filter: neither `on_present` nor `on_missing` set");
  }

  if (rule_.has_on_missing() && rule_.on_missing().value().empty()) {
    throw EnvoyException(
        "payload to metadata filter: cannot specify on_missing rule without non-empty value");
  }

  if (rule_.has_on_present() && rule_.on_present().has_regex_value_rewrite()) {
    const auto& rewrite_spec = rule_.on_present().regex_value_rewrite();
    regex_rewrite_ = Regex::Utility::parseRegex(rewrite_spec.pattern(), regex_engine);
    regex_rewrite_substitution_ = rewrite_spec.substitution();
  }

  switch (rule_.match_specifier_case()) {
  case ProtoRule::MatchSpecifierCase::kMethodName:
    match_type_ = MatchType::MethodName;
    method_or_service_name_ = rule_.method_name();
    break;
  case ProtoRule::MatchSpecifierCase::kServiceName:
    match_type_ = MatchType::ServiceName;
    if (!rule_.service_name().empty() && !absl::EndsWith(rule_.service_name(), ":")) {
      method_or_service_name_ = rule_.service_name() + ":";
    } else {
      method_or_service_name_ = rule_.service_name();
    }
    break;
  case ProtoRule::MatchSpecifierCase::MATCH_SPECIFIER_NOT_SET:
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  const FieldSelector* field_selector = &rule_.field_selector();
  TrieSharedPtr node = root;
  while (true) {
    int16_t id = static_cast<int16_t>(field_selector->id());
    if (node->children_.find(id) == node->children_.end()) {
      node->children_[id] = std::make_shared<Trie>(node);
    }
    node = node->children_[id];
    node->name_ = field_selector->name();
    if (!field_selector->has_child()) {
      break;
    }

    field_selector = &field_selector->child();
  }

  node->rule_ids_.push_back(rule_id_);
}

bool Rule::matches(const ThriftProxy::MessageMetadata& metadata) const {
  if (match_type_ == MatchType::MethodName) {
    const std::string& method_name{method_or_service_name_};
    if (method_name.empty()) {
      return true;
    }

    const std::string& metadata_method_name = metadata.hasMethodName() ? metadata.methodName() : "";
    const auto func_pos = metadata_method_name.find(':');
    if (func_pos != std::string::npos) {
      return metadata_method_name.substr(func_pos + 1) == method_name;
    }
    return metadata_method_name == method_name;
  }
  ASSERT(match_type_ == MatchType::ServiceName);
  const std::string& service_name{method_or_service_name_};
  return service_name.empty() ||
         (metadata.hasMethodName() && absl::StartsWith(metadata.methodName(), service_name));
}

FilterStatus TrieMatchHandler::messageEnd() {
  ASSERT(steps_ == 0);
  ENVOY_LOG(trace, "TrieMatchHandler messageEnd");
  parent_.handleOnMissing();
  complete_ = true;
  return FilterStatus::Continue;
}

FilterStatus TrieMatchHandler::structBegin(absl::string_view) {
  ENVOY_LOG(trace, "TrieMatchHandler structBegin id: {}, steps: {}",
            field_ids_.empty() ? "top_level_struct" : std::to_string(field_ids_.back()), steps_);
  ASSERT(steps_ >= 0);
  assertNode();
  if (!field_ids_.empty()) {
    if (steps_ == 0 && node_->children_.find(field_ids_.back()) != node_->children_.end()) {
      node_ = node_->children_[field_ids_.back()];
      ENVOY_LOG(trace, "name: {}", node_->name_);
    } else {
      steps_++;
    }
  }
  return FilterStatus::Continue;
}

FilterStatus TrieMatchHandler::structEnd() {
  ENVOY_LOG(trace, "TrieMatchHandler structEnd, steps: {}", steps_);
  assertNode();
  if (steps_ > 0) {
    steps_--;
  } else if (node_->parent_.lock()) {
    node_ = node_->parent_.lock();
  } else {
    // last decoder event
    node_ = nullptr;
  }
  ASSERT(steps_ >= 0);
  return FilterStatus::Continue;
}

FilterStatus TrieMatchHandler::fieldBegin(absl::string_view, FieldType&, int16_t& field_id) {
  ENVOY_LOG(trace, "TrieMatchHandler fieldBegin id: {}", field_id);
  field_ids_.push_back(field_id);
  return FilterStatus::Continue;
}

FilterStatus TrieMatchHandler::fieldEnd() {
  ENVOY_LOG(trace, "TrieMatchHandler fieldEnd");
  field_ids_.pop_back();
  return FilterStatus::Continue;
}

FilterStatus TrieMatchHandler::stringValue(absl::string_view value) {
  assertLastFieldId();
  ENVOY_LOG(trace, "TrieMatchHandler stringValue id:{} value:{}", field_ids_.back(), value);
  return handleString(static_cast<std::string>(value));
}

template <typename NumberType> FilterStatus TrieMatchHandler::numberValue(NumberType value) {
  assertLastFieldId();
  ENVOY_LOG(trace, "TrieMatchHandler numberValue id:{} value:{}", field_ids_.back(), value);
  return handleString(std::to_string(value));
}

FilterStatus TrieMatchHandler::handleString(std::string value) {
  ASSERT(steps_ >= 0);
  assertNode();
  assertLastFieldId();
  if (steps_ == 0 && node_->children_.find(field_ids_.back()) != node_->children_.end() &&
      !node_->children_[field_ids_.back()]->rule_ids_.empty()) {
    auto on_present_node = node_->children_[field_ids_.back()];
    ENVOY_LOG(trace, "name: {}", on_present_node->name_);
    parent_.handleOnPresent(std::move(value), on_present_node->rule_ids_);
  }
  return FilterStatus::Continue;
}

void TrieMatchHandler::assertNode() {
  if (node_ == nullptr) {
    throw EnvoyException("payload to metadata filter: invalid trie state, node is null");
  }
}

void TrieMatchHandler::assertLastFieldId() {
  if (field_ids_.empty()) {
    throw EnvoyException("payload to metadata filter: invalid trie state, field_ids_ is null");
  }
}

PayloadToMetadataFilter::PayloadToMetadataFilter(const ConfigSharedPtr config) : config_(config) {}

void PayloadToMetadataFilter::handleOnPresent(std::string&& value,
                                              const std::vector<uint16_t>& rule_ids) {
  for (uint16_t rule_id : rule_ids) {
    if (matched_rule_ids_.find(rule_id) == matched_rule_ids_.end()) {
      ENVOY_LOG(trace, "rule_id {} is not matched.", rule_id);
      continue;
    }
    ENVOY_LOG(trace, "handleOnPresent rule_id {}", rule_id);

    matched_rule_ids_.erase(rule_id);
    ASSERT(rule_id < config_->requestRules().size());
    const Rule& rule = config_->requestRules()[rule_id];
    if (!value.empty() && rule.rule().has_on_present()) {
      // We can *not* always std::move(value) here since we need `value` if multiple rules are
      // matched. Optimize the most common usage, which is one rule per payload field.
      if (rule_ids.size() == 1) {
        applyKeyValue(std::move(value), rule, rule.rule().on_present());
        break;
      } else {
        applyKeyValue(value, rule, rule.rule().on_present());
      }
    }
  }
}

void PayloadToMetadataFilter::handleOnMissing() {
  ENVOY_LOG(trace, "{} rules missing", matched_rule_ids_.size());

  for (uint16_t rule_id : matched_rule_ids_) {
    ENVOY_LOG(trace, "handling on_missing rule_id {}", rule_id);

    ASSERT(rule_id < config_->requestRules().size());
    const Rule& rule = config_->requestRules()[rule_id];
    if (!rule.rule().has_on_missing()) {
      continue;
    }
    applyKeyValue("", rule, rule.rule().on_missing());
  }
}

const std::string& PayloadToMetadataFilter::decideNamespace(const std::string& nspace) const {
  static const std::string& payloadToMetadata = "envoy.filters.thrift.payload_to_metadata";
  return nspace.empty() ? payloadToMetadata : nspace;
}

bool PayloadToMetadataFilter::addMetadata(const std::string& meta_namespace, const std::string& key,
                                          std::string value, ValueType type) {
  ProtobufWkt::Value val;
  ASSERT(!value.empty());

  if (value.size() >= MAX_PAYLOAD_VALUE_LEN) {
    // Too long, go away.
    ENVOY_LOG(error, "metadata value is too long");
    return false;
  }

  ENVOY_LOG(trace, "add metadata ns:{} key:{} value:{}", meta_namespace, key, value);

  // Sane enough, add the key/value.
  switch (type) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::filters::network::thrift_proxy::filters::payload_to_metadata::v3::
      PayloadToMetadata::STRING:
    val.set_string_value(std::move(value));
    break;
  case envoy::extensions::filters::network::thrift_proxy::filters::payload_to_metadata::v3::
      PayloadToMetadata::NUMBER: {
    double dval;
    if (absl::SimpleAtod(StringUtil::trim(value), &dval)) {
      val.set_number_value(dval);
    } else {
      ENVOY_LOG(debug, "value to number conversion failed");
      return false;
    }
    break;
  }
  }

  auto& keyval = structs_by_namespace_[meta_namespace];
  (*keyval.mutable_fields())[key] = std::move(val);

  return true;
}

// add metadata['key']= value depending on payload present or missing case
void PayloadToMetadataFilter::applyKeyValue(std::string value, const Rule& rule,
                                            const KeyValuePair& keyval) {
  if (keyval.has_regex_value_rewrite()) {
    const auto& matcher = rule.regexRewrite();
    value = matcher->replaceAll(value, rule.regexSubstitution());
  } else if (!keyval.value().empty()) {
    value = keyval.value();
  }
  // We do *not* modify value is not present in `on_present` or `on_missing`.
  // That is, we apply the original field value to the metadata.

  if (!value.empty()) {
    const auto& nspace = decideNamespace(keyval.metadata_namespace());
    addMetadata(nspace, keyval.key(), std::move(value), keyval.type());
  } else {
    ENVOY_LOG(debug, "value is empty, not adding metadata");
  }
}

void PayloadToMetadataFilter::finalizeDynamicMetadata() {
  if (!structs_by_namespace_.empty()) {
    for (auto const& entry : structs_by_namespace_) {
      decoder_callbacks_->streamInfo().setDynamicMetadata(entry.first, entry.second);
    }
  }
}

FilterStatus PayloadToMetadataFilter::messageBegin(MessageMetadataSharedPtr metadata) {
  for (const auto& rule : config_->requestRules()) {
    if (rule.matches(*metadata)) {
      ENVOY_LOG(trace, "rule_id {} is matched", rule.ruleId());
      matched_rule_ids_.insert(rule.ruleId());
    }
  }

  ENVOY_LOG(trace, "{} rules matched", matched_rule_ids_.size());
  if (!matched_rule_ids_.empty()) {
    metadata_ = metadata;
  }
  return FilterStatus::Continue;
}

static std::string getHexRepresentation(Buffer::Instance& data) {
  void* buf = data.linearize(static_cast<uint32_t>(data.length()));
  const unsigned char* linearized_data = static_cast<const unsigned char*>(buf);
  std::stringstream hex_stream;
  for (uint32_t i = 0; i < data.length(); ++i) {
    // Convert each byte to a two-digit hexadecimal representation
    hex_stream << std::setfill('0') << std::setw(2) << std::hex
               << static_cast<int>(linearized_data[i]);
    if (i != data.length() - 1) {
      // Append a space after each character except the last one
      hex_stream << ' ';
    }
  }
  std::string hex_representation = hex_stream.str();
  std::transform(hex_representation.begin(), hex_representation.end(), hex_representation.begin(),
                 ::toupper);
  return hex_representation;
}

FilterStatus PayloadToMetadataFilter::passthroughData(Buffer::Instance& data) {
  if (!matched_rule_ids_.empty()) {
    TrieMatchHandler handler(*this, config_->trieRoot());
    ProtocolPtr protocol = createProtocol(decoder_callbacks_->downstreamProtocolType());

    // TODO(kuochunghsu): avoid copying payload https://github.com/envoyproxy/envoy/issues/23901
    Buffer::OwnedImpl data_copy;
    data_copy.add(data);

    DecoderStateMachinePtr state_machine =
        std::make_unique<DecoderStateMachine>(*protocol, metadata_, handler, handler);
    TRY_NEEDS_AUDIT { state_machine->runPassthroughData(data_copy); }
    END_TRY
    CATCH(const EnvoyException& e, {
      IS_ENVOY_BUG(fmt::format("decoding error, error_message: {}, payload: {}", e.what(),
                               getHexRepresentation(data)));
      if (!handler.isComplete()) {
        handleOnMissing();
      }
    });
    finalizeDynamicMetadata();
  }

  return FilterStatus::Continue;
}

} // namespace PayloadToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
