#pragma once

#include <functional>
#include <memory>

#include "envoy/extensions/filters/network/generic_proxy/matcher/v3/matcher.pb.h"
#include "envoy/extensions/filters/network/generic_proxy/matcher/v3/matcher.pb.validate.h"

#include "source/common/matcher/matcher.h"
#include "source/extensions/filters/network/generic_proxy/interface/stream.h"
#include "source/extensions/filters/network/generic_proxy/match_input.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

using ServiceDataInputProto =
    envoy::extensions::filters::network::generic_proxy::matcher::v3::ServiceMatchInput;
using HostDataInputProto =
    envoy::extensions::filters::network::generic_proxy::matcher::v3::HostMatchInput;
using PathDataInputProto =
    envoy::extensions::filters::network::generic_proxy::matcher::v3::PathMatchInput;
using MethodDataInputProto =
    envoy::extensions::filters::network::generic_proxy::matcher::v3::MethodMatchInput;
using PropertyDataInputProto =
    envoy::extensions::filters::network::generic_proxy::matcher::v3::PropertyMatchInput;
using RequestInputProto =
    envoy::extensions::filters::network::generic_proxy::matcher::v3::RequestMatchInput;
using RequestMatcherProto =
    envoy::extensions::filters::network::generic_proxy::matcher::v3::RequestMatcher;
using StringMatcherProto = envoy::type::matcher::v3::StringMatcher;

// Fully qualified name of the generic proxy request match data type to avoid any possible
// collision with other match data types.
inline constexpr absl::string_view GenericRequestMatcheInputType =
    "Envoy::Extensions::NetworkFilters::GenericProxy::RequestMatchData";

class ServiceMatchDataInput : public Matcher::DataInput<MatchInput> {
public:
  Matcher::DataInputGetResult get(const MatchInput& data) const override {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            std::string(data.requestHeader().host())};
  }
};

class ServiceMatchDataInputFactory : public Matcher::DataInputFactory<MatchInput> {
public:
  ServiceMatchDataInputFactory() = default;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ServiceDataInputProto>();
  }

  Matcher::DataInputFactoryCb<MatchInput>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<ServiceMatchDataInput>(); };
  }

  std::string name() const override { return "envoy.matching.generic_proxy.input.service"; }
};

class HostMatchDataInput : public Matcher::DataInput<MatchInput> {
public:
  Matcher::DataInputGetResult get(const MatchInput& data) const override {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            std::string(data.requestHeader().host())};
  }
};

class HostMatchDataInputFactory : public Matcher::DataInputFactory<MatchInput> {
public:
  HostMatchDataInputFactory() = default;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<HostDataInputProto>();
  }

  Matcher::DataInputFactoryCb<MatchInput>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<HostMatchDataInput>(); };
  }

  std::string name() const override { return "envoy.matching.generic_proxy.input.host"; }
};

class PathMatchDataInput : public Matcher::DataInput<MatchInput> {
public:
  Matcher::DataInputGetResult get(const MatchInput& data) const override {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            std::string(data.requestHeader().path())};
  }
};

class PathMatchDataInputFactory : public Matcher::DataInputFactory<MatchInput> {
public:
  PathMatchDataInputFactory() = default;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<PathDataInputProto>();
  }

  Matcher::DataInputFactoryCb<MatchInput>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<PathMatchDataInput>(); };
  }

  std::string name() const override { return "envoy.matching.generic_proxy.input.path"; }
};

class MethodMatchDataInput : public Matcher::DataInput<MatchInput> {
public:
  Matcher::DataInputGetResult get(const MatchInput& data) const override {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            std::string(data.requestHeader().method())};
  }
};

class MethodMatchDataInputFactory : public Matcher::DataInputFactory<MatchInput> {
public:
  MethodMatchDataInputFactory() = default;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<MethodDataInputProto>();
  }

  Matcher::DataInputFactoryCb<MatchInput>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<MethodMatchDataInput>(); };
  }

  std::string name() const override { return "envoy.matching.generic_proxy.input.method"; }
};

class PropertyMatchDataInput : public Matcher::DataInput<MatchInput> {
public:
  PropertyMatchDataInput(const std::string& property_name) : name_(property_name) {}

  Matcher::DataInputGetResult get(const MatchInput& data) const override {
    const auto value = data.requestHeader().get(name_);
    Matcher::MatchingDataType matching_data =
        value.has_value() ? Matcher::MatchingDataType(std::string(value.value()))
                          : absl::monostate();
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, matching_data};
  }

private:
  const std::string name_;
};

class PropertyMatchDataInputFactory : public Matcher::DataInputFactory<MatchInput> {
public:
  PropertyMatchDataInputFactory() = default;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<PropertyDataInputProto>();
  }

  Matcher::DataInputFactoryCb<MatchInput>
  createDataInputFactoryCb(const Protobuf::Message& message,
                           ProtobufMessage::ValidationVisitor& visitor) override {
    const auto& config =
        MessageUtil::downcastAndValidate<const PropertyDataInputProto&>(message, visitor);
    const std::string name = config.property_name();

    return [name]() { return std::make_unique<PropertyMatchDataInput>(name); };
  }

  std::string name() const override { return "envoy.matching.generic_proxy.input.property"; }
};

// RequestMatchData is a wrapper of Request to be used as the matching data type.
class RequestMatchData : public Matcher::CustomMatchData {
public:
  RequestMatchData(const MatchInput& data) : data_(data) {}

  const MatchInput& data() const { return data_; }

private:
  const MatchInput& data_;
};

class RequestMatchDataInput : public Matcher::DataInput<MatchInput> {
public:
  RequestMatchDataInput() = default;

  Matcher::DataInputGetResult get(const MatchInput& data) const override {
    auto request = std::make_shared<RequestMatchData>(data);
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            Matcher::MatchingDataType{std::move(request)}};
  }

  absl::string_view dataInputType() const override { return GenericRequestMatcheInputType; }
};

class RequestMatchDataInputFactory : public Matcher::DataInputFactory<MatchInput> {
public:
  RequestMatchDataInputFactory() = default;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<RequestInputProto>();
  }

  Matcher::DataInputFactoryCb<MatchInput>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<RequestMatchDataInput>(); };
  }

  std::string name() const override { return "envoy.matching.generic_proxy.input.request"; }
};

class RequestMatchInputMatcher : public Matcher::InputMatcher {
public:
  RequestMatchInputMatcher(const RequestMatcherProto& config,
                           Server::Configuration::CommonFactoryContext& context);

  bool match(const Matcher::MatchingDataType& input) override;
  bool match(const RequestHeaderFrame& request);

  absl::flat_hash_set<std::string> supportedDataInputTypes() const override {
    return absl::flat_hash_set<std::string>{std::string(GenericRequestMatcheInputType)};
  }

private:
  Matchers::StringMatcherPtr host_;
  Matchers::StringMatcherPtr path_;
  Matchers::StringMatcherPtr method_;
  std::vector<std::pair<std::string, Matchers::StringMatcherPtr>> properties_;
};

class RequestMatchDataInputMatcherFactory : public Matcher::InputMatcherFactory {
public:
  Matcher::InputMatcherFactoryCb createInputMatcherFactoryCb(
      const Protobuf::Message& config,
      Server::Configuration::ServerFactoryContext& factory_context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<RequestMatcherProto>();
  }

  std::string name() const override {
    return "envoy.matching.input_matchers.generic_request_matcher";
  }
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
