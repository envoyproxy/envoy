#pragma once

#include "envoy/common/hashable.h"
#include "envoy/extensions/filters/common/set_filter_state/v3/value.pb.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace SetFilterState {

using LifeSpan = StreamInfo::FilterState::LifeSpan;
using StateType = StreamInfo::FilterState::StateType;
using StreamSharing = StreamInfo::StreamSharingMayImpactPooling;
using FilterStateValueProto =
    envoy::extensions::filters::common::set_filter_state::v3::FilterStateValue;

struct Value {
  std::string key_;
  const StreamInfo::FilterState::ObjectFactory* factory_;
  StateType state_type_{StateType::ReadOnly};
  StreamSharing stream_sharing_{StreamSharing::None};
  bool skip_if_empty_;
  Formatter::FormatterConstSharedPtr value_;
};

class HashableStringObject : public StreamInfo::FilterState::Object, public Hashable {
public:
  HashableStringObject(absl::string_view value) : value_(value) {}

  // StringAccessor
  absl::string_view asString() const { return value_; }

  // FilterState::Object
  ProtobufTypes::MessagePtr serializeAsProto() const override {
    auto message = std::make_unique<ProtobufWkt::StringValue>();
    message->set_value(value_);
    return message;
  }
  absl::optional<std::string> serializeAsString() const override { return value_; }

  // Implements hashing interface because the value is applied once per upstream connection.
  // Multiple streams sharing the upstream connection must have the same object.
  absl::optional<uint64_t> hash() const override { return HashUtil::xxHash64(value_); }

private:
  std::string value_;
};

class Config : public Logger::Loggable<Logger::Id::config> {
public:
  Config(const Protobuf::RepeatedPtrField<FilterStateValueProto>& proto_values, LifeSpan life_span,
         Server::Configuration::GenericFactoryContext& context)
      : life_span_(life_span), values_(parse(proto_values, context)) {}
  void updateFilterState(const Formatter::HttpFormatterContext& context,
                         StreamInfo::StreamInfo& info) const;

private:
  std::vector<Value> parse(const Protobuf::RepeatedPtrField<FilterStateValueProto>& proto_values,
                           Server::Configuration::GenericFactoryContext& context) const;
  const LifeSpan life_span_;
  const std::vector<Value> values_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

} // namespace SetFilterState
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
