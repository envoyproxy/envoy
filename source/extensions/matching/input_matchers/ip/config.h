#pragma once

#include "envoy/extensions/matching/input_matchers/ip/v3/ip.pb.h"
#include "envoy/extensions/matching/input_matchers/ip/v3/ip.pb.validate.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

#include "common/protobuf/utility.h"

#include "extensions/matching/input_matchers/ip/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace IP {

class IPConfig : public Envoy::Matcher::InputMatcherFactory {
public:
  Envoy::Matcher::InputMatcherPtr
  createInputMatcher(const Protobuf::Message& config,
                     Server::Configuration::FactoryContext& factory_context) override;

  std::string name() const override { return "envoy.matching.matchers.ip"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::matching::input_matchers::ip::v3::Ip>();
  }
};
} // namespace IP
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
