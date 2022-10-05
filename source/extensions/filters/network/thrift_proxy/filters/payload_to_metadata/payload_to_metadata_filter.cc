#include "source/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/payload_to_metadata_filter.h"

#include "source/common/common/base64.h"
#include "source/common/common/regex.h"
#include "source/common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace PayloadToMetadataFilter {

using namespace Envoy::Extensions::NetworkFilters;

Config::Config(const envoy::extensions::filters::network::thrift_proxy::filters::
                   payload_to_metadata::v3::PayloadToMetadata& config) {
  trie_root_ = std::make_shared<Trie>();
  for (const auto& entry : config.request_rules()) {
    request_rules_.emplace_back(entry, request_rules_.size(), trie_root_);
  }
}

Rule::Rule(const ProtoRule& rule, uint16_t id, TrieSharedPtr root) : rule_(rule), id_(id) {
  if (!rule.has_on_present() && !rule.has_on_missing()) {
    throw EnvoyException("payload to metadata filter: neither `on_present` nor `on_missing` set");
  }

  if (rule.has_on_missing() && rule.on_missing().value().empty()) {
    throw EnvoyException(
        "payload to metadata filter: cannot specify on_missing rule without non-empty value");
  }

  if (rule.has_on_present() && rule.on_present().has_regex_value_rewrite()) {
    const auto& rewrite_spec = rule.on_present().regex_value_rewrite();
    regex_rewrite_ = Regex::Utility::parseRegex(rewrite_spec.pattern());
    regex_rewrite_substitution_ = rewrite_spec.substitution();
  }

  switch (rule.match_specifier_case()) {
  case ProtoRule::MatchSpecifierCase::kMethodName:
    match_type_ = MatchType::METHOD_NAME;
    method_or_service_name_ = rule.method_name();
    break;
  case ProtoRule::MatchSpecifierCase::kServiceName:
    match_type_ = MatchType::SERVICE_NAME;
    if (!rule.service_name().empty() && !absl::EndsWith(rule.service_name(), ":")) {
      method_or_service_name_ = rule.service_name() + ":";
    } else {
      method_or_service_name_ = rule.service_name();
    }
    break;
  case ProtoRule::MatchSpecifierCase::MATCH_SPECIFIER_NOT_SET:
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  auto field_selector = rule.field_selector();
  TrieSharedPtr node = root;
  while (true) {
    uint32_t id = field_selector.id().value();
    if (node->children_.find(id) == node->children_.end()) {
      node->children_[id] = std::make_shared<Trie>(node);
    }
    node = node->children_[id];
    node->name_ = field_selector.name();
    if (!field_selector.has_child()) {
      break;
    }
    field_selector = field_selector.child();
  }
  ASSERT(node != root);
  node->rule_ = *this;
}

bool Rule::matches(const ThriftProxy::MessageMetadata& metadata) const {
  if (match_type_ == MatchType::METHOD_NAME) {
    return method_or_service_name_.empty() ||
           (metadata.hasMethodName() && metadata.methodName() == method_or_service_name_);
  }
  ASSERT(match_type_ == MatchType::SERVICE_NAME);
  return method_or_service_name_.empty() ||
         (metadata.hasMethodName() &&
          absl::StartsWith(metadata.methodName(), method_or_service_name_));
}

FilterStatus TrieMatchHandler::messageEnd() {
  ENVOY_LOG(trace, "TrieMatchHandler messageEnd");
  complete_ = true;
  return FilterStatus::Continue;
}

PayloadToMetadataFilter::PayloadToMetadataFilter(const ConfigSharedPtr config) : config_(config) {}

FilterStatus PayloadToMetadataFilter::messageBegin(MessageMetadataSharedPtr metadata) {
  for (const auto& rule : config_->requestRules()) {
    if (rule.matches(*metadata)) {
      matched_ids_.insert(rule.id());
    }
  }

  ENVOY_LOG(trace, "{} rules matched", matched_ids_.size());
  return FilterStatus::Continue;
}

FilterStatus PayloadToMetadataFilter::passthroughData(Buffer::Instance& data) {
  if (!matched_ids_.empty()) {
    ASSERT(!decoder_);
    handler_ = std::make_unique<TrieMatchHandler>(config_->trieRoot());
    transport_ = createTransport(decoder_callbacks_->downstreamTransportType());
    protocol_ = createProtocol(decoder_callbacks_->downstreamProtocolType());
    decoder_ = std::make_unique<Decoder>(*transport_, *protocol_, *handler_);

    bool underflow = false;
    decoder_->onData(data, underflow);
    ASSERT(handler_->isComplete() || underflow);
  }

  return FilterStatus::Continue;
}

} // namespace PayloadToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
