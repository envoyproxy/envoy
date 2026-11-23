#pragma once

#include <memory>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/matcher/matcher.h"

#include "source/common/common/fmt.h"
#include "source/common/protobuf/protobuf.h"

#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
#include "xds/type/matcher/v3/matcher.pb.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

// An HTTP input that is not in the RBAC filter's built-in inputs. It returns the value of its
// google.protobuf.StringValue config and is registered while in scope.
class TestExtensionInputFactory : public Envoy::Matcher::DataInputFactory<Http::HttpMatchingData> {
public:
  explicit TestExtensionInputFactory(Envoy::Matcher::DataInputStage stage)
      : stage_(stage), injection_(*this) {}

  Envoy::Matcher::DataInputFactoryCb<Http::HttpMatchingData>
  createDataInputFactoryCb(const Protobuf::Message& config,
                           ProtobufMessage::ValidationVisitor&) override {
    const std::string value =
        Envoy::Protobuf::DynamicCastMessage<Protobuf::StringValue>(config).value();
    return [value] { return std::make_unique<Input>(value); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::StringValue>();
  }

  std::string name() const override { return "test_extension_input"; }

  Envoy::Matcher::DataInputStage availableAt() const override { return stage_; }

private:
  struct Input : public Envoy::Matcher::DataInput<Http::HttpMatchingData> {
    explicit Input(std::string value) : value_(std::move(value)) {}

    Envoy::Matcher::DataInputGetResult get(const Http::HttpMatchingData&) const override {
      return Envoy::Matcher::DataInputGetResult::CreateStringView(value_);
    }

    const std::string value_;
  };

  const Envoy::Matcher::DataInputStage stage_;
  Registry::InjectFactory<Envoy::Matcher::DataInputFactory<Http::HttpMatchingData>> injection_;
};

// A matcher that allows when TestExtensionInputFactory's input, configured with input_value, is
// "foo".
inline xds::type::matcher::v3::Matcher extensionInputMatcher(absl::string_view input_value) {
  return TestUtility::parseYaml<xds::type::matcher::v3::Matcher>(fmt::format(R"EOF(
matcher_list:
  matchers:
  - predicate:
      single_predicate:
        input:
          name: test_extension_input
          typed_config:
            '@type': type.googleapis.com/google.protobuf.StringValue
            value: "{}"
        value_match:
          exact: foo
    on_match:
      action:
        name: action
        typed_config:
          '@type': type.googleapis.com/envoy.config.rbac.v3.Action
          name: allow
          action: ALLOW
)EOF",
                                                                             input_value));
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
