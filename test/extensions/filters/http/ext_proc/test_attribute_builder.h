#pragma once

#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/http/ext_proc/attribute_builder/attribute_builder.h"
#include "source/extensions/filters/http/ext_proc/matching_utils.h"

#include "test/extensions/filters/http/ext_proc/test_attribute_builder.pb.h"
#include "test/extensions/filters/http/ext_proc/test_attribute_builder.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class TestAttributeBuilder : public AttributeBuilder {
public:
  using TestAttributeBuilderConfig =
      envoy::test::extensions::filters::http::ext_proc::v3::TestAttributeBuilderConfig;

  TestAttributeBuilder(const TestAttributeBuilderConfig& config,
                       Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
                       Server::Configuration::CommonFactoryContext& context);

  absl::optional<Protobuf::Struct> build(const BuildParams& params) const override;

private:
  const TestAttributeBuilderConfig config_;
  const ExpressionManager expression_manager_;
};

class TestAttributeBuilderFactory : public AttributeBuilderFactory {
public:
  using TestAttributeBuilderConfig =
      envoy::test::extensions::filters::http::ext_proc::v3::TestAttributeBuilderConfig;

  ~TestAttributeBuilderFactory() override = default;
  std::unique_ptr<AttributeBuilder> createAttributeBuilder(
      const Protobuf::Message& config,
      Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
      Envoy::Server::Configuration::CommonFactoryContext& context) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<TestAttributeBuilderConfig>();
  }

  std::string name() const override { return "test_attribute_builder"; }
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
