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
          input_config)
      : filter_(input_config.filter()), path_(initializePath(input_config.path())) {}

  Matcher::DataInputGetResult get(const MatchingDataType& data) const override {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            std::make_unique<MetadataMatchData>(
                Envoy::Config::Metadata::metadataValue(&data.metadata(), filter_, path_))};
  }

private:
  static std::vector<std::string> initializePath(
      const Protobuf::RepeatedPtrField<envoy::extensions::matching::common_inputs::network::v3::
                                           DynamicMetadataInput::PathSegment>& segments) {
    std::vector<std::string> path;
    for (const auto& seg : segments) {
      path.push_back(seg.key());
    }
    return path;
  }

  const std::string filter_;
  const std::vector<std::string> path_;
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
    auto config_ptr = std::make_shared<
        envoy::extensions::matching::common_inputs::network::v3::DynamicMetadataInput>(
        typed_config);
    return [config_ptr] {
      return std::make_unique<DynamicMetadataInput<MatchingDataType>>(*config_ptr);
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
