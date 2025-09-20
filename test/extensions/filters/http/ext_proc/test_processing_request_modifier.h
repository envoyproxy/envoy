#pragma once

#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/http/ext_proc/matching_utils.h"
#include "source/extensions/filters/http/ext_proc/processing_request_modifier.h"

#include "test/extensions/filters/http/ext_proc/test_processing_request_modifier.pb.h"
#include "test/extensions/filters/http/ext_proc/test_processing_request_modifier.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class TestProcessingRequestModifier : public ProcessingRequestModifier {
public:
  TestProcessingRequestModifier(
      const envoy::test::extensions::filters::http::ext_proc::v3::
          TestProcessingRequestModifierConfig& config,
      Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
      Server::Configuration::CommonFactoryContext& context);

  bool run(const Params& params, envoy::service::ext_proc::v3::ProcessingRequest& request) override;

private:
  const envoy::test::extensions::filters::http::ext_proc::v3::TestProcessingRequestModifierConfig
      config_;
  const ExpressionManager expression_manager_;
  bool sent_request_attributes_ = false;
};

class TestProcessingRequestModifierFactory : public ProcessingRequestModifierFactory {
public:
  ~TestProcessingRequestModifierFactory() override = default;
  std::unique_ptr<ProcessingRequestModifier> createProcessingRequestModifier(
      const Protobuf::Message& config,
      Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
      Envoy::Server::Configuration::CommonFactoryContext& context) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::test::extensions::filters::http::ext_proc::v3::
                                TestProcessingRequestModifierConfig>();
  }

  std::string name() const override { return "test_processing_request_modifier"; }
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
