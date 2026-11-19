#pragma once

#include <memory>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/matcher/matcher.h"

#include "source/common/protobuf/protobuf.h"

#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "xds/type/matcher/v3/matcher.pb.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

// An HTTP input that is not in the RBAC filter's built-in inputs, configured with a
// google.protobuf.StringValue. It is registered while in scope.
class TestExtensionInputFactory : public Envoy::Matcher::DataInputFactory<Http::HttpMatchingData> {
public:
  explicit TestExtensionInputFactory(Envoy::Matcher::DataInputStage stage)
      : stage_(stage), injection_(*this) {}

  Envoy::Matcher::DataInputFactoryCb<Http::HttpMatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return [] { return std::make_unique<Input>(); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::StringValue>();
  }

  std::string name() const override { return "test_extension_input"; }

  Envoy::Matcher::DataInputStage availableAt() const override { return stage_; }

private:
  struct Input : public Envoy::Matcher::DataInput<Http::HttpMatchingData> {
    Envoy::Matcher::DataInputGetResult get(const Http::HttpMatchingData&) const override {
      return Envoy::Matcher::DataInputGetResult::NoData();
    }
  };

  const Envoy::Matcher::DataInputStage stage_;
  Registry::InjectFactory<Envoy::Matcher::DataInputFactory<Http::HttpMatchingData>> injection_;
};

// A matcher with one ALLOW rule on TestExtensionInputFactory's input.
inline xds::type::matcher::v3::Matcher extensionInputMatcher() {
  return TestUtility::parseYaml<xds::type::matcher::v3::Matcher>(R"EOF(
matcher_list:
  matchers:
  - predicate:
      single_predicate:
        input:
          name: test_extension_input
          typed_config:
            '@type': type.googleapis.com/google.protobuf.StringValue
        value_match:
          exact: foo
    on_match:
      action:
        name: action
        typed_config:
          '@type': type.googleapis.com/envoy.config.rbac.v3.Action
          name: allow
          action: ALLOW
)EOF");
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
