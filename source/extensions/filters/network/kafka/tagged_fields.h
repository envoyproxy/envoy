#pragma once

#include <vector>

#include "extensions/filters/network/kafka/serialization.h"

/**
 * This header file provides serialization support for tagged fields structure added in 2.4.
 * https://github.com/apache/kafka/blob/2.4.0/clients/src/main/java/org/apache/kafka/common/protocol/types/TaggedFields.java
 *
 * Impl note: contrary to other compact data structures, data in tagged field does not have +1 in
 * data length.
 */

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Simple data-holding structure.
 */
struct TaggedField {

  uint32_t tag_;
  std::vector<unsigned char> data_;

  uint32_t computeSize(const EncodingContext&) const {
    // FIXME(adam.kotwasinski)
    throw std::runtime_error("not implemented");
  }

  uint32_t encode(Buffer::Instance&, EncodingContext&) const {
    // FIXME(adam.kotwasinski)
    throw std::runtime_error("not implemented");
  }

  bool operator==(const TaggedField& rhs) const { return tag_ == rhs.tag_ && data_ == rhs.data_; }
};

/**
 * Deserializer responsible for extracting a TaggedField from data provided.
 */
class TaggedFieldDeserializer : public Deserializer<TaggedField> {
public:
  TaggedFieldDeserializer() = default;

  uint32_t feed(absl::string_view& data) override {
    uint32_t consumed = 0;
    consumed += tag_deserializer_.feed(data);
    consumed += length_deserializer_.feed(data);

    if (!length_deserializer_.ready()) {
      return consumed;
    }

    if (!length_consumed_) {
      required_ = length_deserializer_.get();
      data_buffer_ = std::vector<unsigned char>(required_);
      length_consumed_ = true;
    }

    const uint32_t data_consumed = std::min<uint32_t>(required_, data.size());
    const uint32_t written = data_buffer_.size() - required_;
    if (data_consumed > 0) {
      memcpy(data_buffer_.data() + written, data.data(), data_consumed);
      required_ -= data_consumed;
      data = {data.data() + data_consumed, data.size() - data_consumed};
    }

    if (required_ == 0) {
      ready_ = true;
    }

    return consumed;
  };

  bool ready() const override { return ready_; };

  TaggedField get() const override { return {tag_deserializer_.get(), data_buffer_}; };

private:
  VarUInt32Deserializer tag_deserializer_;
  VarUInt32Deserializer length_deserializer_;
  bool length_consumed_{false};
  uint32_t required_;
  std::vector<unsigned char> data_buffer_;
  bool ready_{false};
};

/**
 * Aggregate of multiple TaggedField objects.
 */
struct TaggedFields {

  const std::vector<TaggedField> fields_;

  uint32_t computeSize(const EncodingContext&) const {
    // FIXME(adam.kotwasinski)
    throw std::runtime_error("not implemented");
  }

  uint32_t encode(Buffer::Instance&, EncodingContext&) const {
    // FIXME(adam.kotwasinski)
    throw std::runtime_error("not implemented");
  }

  bool operator==(const TaggedFields& rhs) const { return fields_ == rhs.fields_; }
};

/**
 * Deserializer responsible for extracting tagged fields from data provided.
 */
class TaggedFieldsDeserializer : public Deserializer<TaggedFields> {
public:
  uint32_t feed(absl::string_view& data) override {

    const uint32_t count_consumed = count_deserializer_.feed(data);
    if (!count_deserializer_.ready()) {
      return count_consumed;
    }

    if (!children_setup_) {
      const uint32_t field_count = count_deserializer_.get();
      children_ = std::vector<TaggedFieldDeserializer>(field_count);
      children_setup_ = true;
    }

    if (ready_) {
      return count_consumed;
    }

    uint32_t child_consumed{0};
    for (TaggedFieldDeserializer& child : children_) {
      child_consumed += child.feed(data);
    }

    bool children_ready_ = true;
    for (TaggedFieldDeserializer& child : children_) {
      children_ready_ &= child.ready();
    }
    ready_ = children_ready_;

    return count_consumed + child_consumed;
  };

  bool ready() const override { return ready_; };

  TaggedFields get() const override {
    std::vector<TaggedField> fields{};
    fields.reserve(children_.size());
    for (const TaggedFieldDeserializer& child : children_) {
      const TaggedField child_result = child.get();
      fields.push_back(child_result);
    }
    return {fields};
  };

private:
  VarUInt32Deserializer count_deserializer_;
  std::vector<TaggedFieldDeserializer> children_;

  bool children_setup_ = false;
  bool ready_ = false;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
