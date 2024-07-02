#pragma once

#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.h"
#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/matcher/matcher.h"

#include "source/common/config/metadata.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Http {
namespace MetadataInput {

class MetadataMatchData : public ::Envoy::Matcher::CustomMatchData {
public:
  explicit MetadataMatchData(const ProtobufWkt::Value& value) : value_(value) {}
  const ProtobufWkt::Value& value_;
};

template <class MatchingDataType>
class DynamicMetadataInput : public Matcher::DataInput<MatchingDataType> {
public:
  DynamicMetadataInput(
      const envoy::extensions::matching::common_inputs::network::v3::DynamicMetadataInput&
          inputConfig)
      : filter_(inputConfig.filter()) {
    for (const auto& seg : inputConfig.path()) {
      path_.push_back(seg.key());
    }
  }

  Matcher::DataInputGetResult get(const MatchingDataType& data) const override {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            std::make_unique<MetadataMatchData>(
                Envoy::Config::Metadata::metadataValue(&data.metadata(), filter_, path_))};
  }

private:
  const std::string filter_;
  const std::string metadata_key_;
  std::vector<std::string> path_;
};

template <class MatchingDataType>
class DynamicMetadataInputBaseFactory : public Matcher::DataInputFactory<MatchingDataType> {
public:
  std::string name() const override { return "envoy.matching.inputs.dynamic_metadata"; }

  Matcher::DataInputFactoryCb<MatchingDataType>
  createDataInputFactoryCb(const Protobuf::Message& message,
                           ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& typed_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::matching::common_inputs::network::v3::DynamicMetadataInput&>(
        message, validation_visitor);

    return [input = typed_config] {
      return std::make_unique<DynamicMetadataInput<MatchingDataType>>(input);
    };
  };

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::common_inputs::network::v3::DynamicMetadataInput>();
  }
};

DECLARE_FACTORY(HttpDymanicMetadataInputFactory);

} // namespace MetadataInput
} // namespace Http
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
