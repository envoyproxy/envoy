#include "source/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/payload_extractor.h"

#include "source/common/common/regex.h"
#include "source/extensions/filters/network/thrift_proxy/auto_protocol_impl.h"
#include "source/extensions/filters/network/thrift_proxy/auto_transport_impl.h"
#include "source/extensions/filters/network/thrift_proxy/binary_protocol_impl.h"
#include "source/extensions/filters/network/thrift_proxy/compact_protocol_impl.h"
#include "source/extensions/filters/network/thrift_proxy/framed_transport_impl.h"
#include "source/extensions/filters/network/thrift_proxy/header_transport_impl.h"
#include "source/extensions/filters/network/thrift_proxy/unframed_transport_impl.h"

namespace Envoy {
namespace Extensions {
namespace PayloadExtractor {

using namespace Envoy::Extensions::NetworkFilters;

FilterStatus TrieMatchHandler::messageBegin(MessageMetadataSharedPtr metadata) {
  ENVOY_LOG(trace, "TrieMatchHandler messageBegin");
  return parent_.handleThriftMetadata(metadata);
}

FilterStatus TrieMatchHandler::messageEnd() {
  ASSERT(steps_ == 0);
  ENVOY_LOG(trace, "TrieMatchHandler messageEnd");
  complete_ = true;
  parent_.handleComplete(is_request_);
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
  return handleValue(value);
}

FilterStatus TrieMatchHandler::numberValue(int64_t value) {
  assertLastFieldId();
  ENVOY_LOG(trace, "TrieMatchHandler numberValue id:{} value:{}", field_ids_.back(), value);
  return handleValue(value);
}

FilterStatus TrieMatchHandler::doubleValue(double& value) {
  assertLastFieldId();
  ENVOY_LOG(trace, "TrieMatchHandler doubleValue id:{} value:{}", field_ids_.back(), value);
  return handleValue(value);
}

FilterStatus
TrieMatchHandler::handleValue(absl::variant<absl::string_view, int64_t, double> value) {
  ASSERT(steps_ >= 0);
  assertNode();
  assertLastFieldId();
  if (steps_ == 0 && node_->children_.find(field_ids_.back()) != node_->children_.end() &&
      !node_->children_[field_ids_.back()]->rule_ids_.empty()) {
    auto on_present_node = node_->children_[field_ids_.back()];
    ENVOY_LOG(trace, "name: {}", on_present_node->name_);
    parent_.handleOnPresent(std::move(value), on_present_node->rule_ids_, is_request_);
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

} // namespace PayloadExtractor
} // namespace Extensions
} // namespace Envoy
