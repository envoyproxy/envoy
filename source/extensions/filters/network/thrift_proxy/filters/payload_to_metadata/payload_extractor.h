#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "source/extensions/filters/network/thrift_proxy/decoder.h"
#include "source/extensions/filters/network/thrift_proxy/filters/pass_through_filter.h"

#include "absl/container/node_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace PayloadExtractor {

using namespace Envoy::Extensions::NetworkFilters;
using namespace Envoy::Extensions::NetworkFilters::ThriftProxy;

class Trie;
using TrieSharedPtr = std::shared_ptr<Trie>;

class Trie : public std::enable_shared_from_this<Trie> {
public:
  Trie(TrieSharedPtr parent = nullptr) : parent_(parent) {}

  // Insert a new field selector into the trie
  template <typename FieldSelector>
  void insert(const FieldSelector* field_selector, uint16_t rule_id) {
    PayloadExtractor::TrieSharedPtr node = shared_from_this();
    while (true) {
      int16_t id = static_cast<int16_t>(field_selector->id());
      if (node->children_.find(id) == node->children_.end()) {
        node->children_[id] = std::make_shared<PayloadExtractor::Trie>(node);
      }
      node = node->children_[id];
      node->name_ = field_selector->name();
      if (!field_selector->has_child()) {
        break;
      }

      field_selector = &field_selector->child();
    }

    node->rule_ids_.push_back(rule_id);
  }

private:
  // TODO(JuniorHsu): remove this friend declaration
  friend class TrieMatchHandler;
  std::string name_;
  std::weak_ptr<Trie> parent_;
  // Field ID to payload node
  absl::node_hash_map<int16_t, TrieSharedPtr> children_;
  std::vector<uint16_t> rule_ids_;
};

class MetadataHandler {
public:
  virtual ~MetadataHandler() = default;
  virtual FilterStatus handleThriftMetadata(MessageMetadataSharedPtr metadata) PURE;
  virtual void handleOnPresent(absl::variant<absl::string_view, int64_t, double> value,
                               const std::vector<uint16_t>& rule_ids, bool is_request) PURE;
  virtual void handleComplete(bool is_request) PURE;
};

class TrieMatchHandler : public DecoderCallbacks,
                         public PassThroughDecoderEventHandler,
                         protected Logger::Loggable<Envoy::Logger::Id::thrift> {
public:
  TrieMatchHandler(MetadataHandler& parent, TrieSharedPtr root, bool is_request = true)
      : parent_(parent), node_(root), is_request_(is_request) {}

  // DecoderEventHandler
  FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
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
  FilterStatus doubleValue(double& value) override;
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
  FilterStatus numberValue(int64_t value);
  FilterStatus handleValue(absl::variant<absl::string_view, int64_t, double> value);
  void assertNode();
  void assertLastFieldId();

  FilterStatus handleContainerBegin() {
    steps_++;
    return FilterStatus::Continue;
  }

  FilterStatus handleContainerEnd() {
    ASSERT(steps_ > 0, "unmatched container end");
    steps_--;
    return FilterStatus::Continue;
  }

  MetadataHandler& parent_;
  TrieSharedPtr node_;
  bool is_request_{true};
  bool complete_{false};
  std::vector<int16_t> field_ids_;
  int16_t steps_{0};
};

using TrieMatchHandlerPtr = std::unique_ptr<TrieMatchHandler>;

} // namespace PayloadExtractor
} // namespace Extensions
} // namespace Envoy
