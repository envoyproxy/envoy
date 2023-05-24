#pragma once

#include <memory>
#include <string>

#include "envoy/matcher/matcher.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/matching/input_matchers/cel_matcher/matcher.h"

#include "xds/type/matcher/v3/cel.pb.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace CelMatcher {

class CelInputMatcherFactory : public InputMatcherFactory {
public:
  InputMatcherFactoryCb
  createInputMatcherFactoryCb(const Protobuf::Message& config,
                              Server::Configuration::ServerFactoryContext&) override {
    const auto& cel_matcher = dynamic_cast<const ::xds::type::matcher::v3::CelMatcher&>(config);

    return [cel_matcher = std::move(cel_matcher)] {
      return std::make_unique<CelInputMatcher>(cel_matcher.expr_match());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<CelMatcher>();
  }

  std::string name() const override { return "envoy.matching.matchers.cel_matcher"; }
};

} // namespace CelMatcher
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
