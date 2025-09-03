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
  TestAttributeBuilder(
      const envoy::test::extensions::filters::http::ext_proc::v3::TestAttributeBuilderConfig&
          config,
      absl::string_view default_attribute_key,
      Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
      Server::Configuration::CommonFactoryContext& context);

  bool build(const BuildParams& params,
             Protobuf::Map<std::string, Protobuf::Struct>* attributes) const override;

private:
  const envoy::test::extensions::filters::http::ext_proc::v3::TestAttributeBuilderConfig config_;
  const std::string default_attribute_key_;
  const ExpressionManager expression_manager_;
};

class TestAttributeBuilderFactory : public AttributeBuilderFactory {
public:
  ~TestAttributeBuilderFactory() override = default;
  std::unique_ptr<AttributeBuilder> createAttributeBuilder(
      const Protobuf::Message& config, absl::string_view default_attribute_key,
      Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
      Envoy::Server::Configuration::CommonFactoryContext& context) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::test::extensions::filters::http::ext_proc::v3::TestAttributeBuilderConfig>();
  }

  std::string name() const override { return "test_attribute_builder"; }
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
