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

struct Trie;
using TrieSharedPtr = std::shared_ptr<Trie>;

struct Trie {
  Trie(TrieSharedPtr parent = nullptr) : parent_(parent) {}
  std::string name_;
  std::weak_ptr<Trie> parent_;
  // Field ID to payload node
  absl::node_hash_map<int16_t, TrieSharedPtr> children_;
  std::vector<uint16_t> rule_ids_;
};

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
  bool complete_{false};
  std::vector<int16_t> field_ids_;
  int16_t steps_{0};
};

} // namespace PayloadExtractor
} // namespace Extensions
} // namespace Envoy
