#pragma once

#include "source/extensions/filters/http/ext_proc/processing_request_modifier.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class TestProcessingRequestModifier : public ProcessingRequestModifier {
public:
  TestProcessingRequestModifier() = default;

  bool modifyRequest(const Params&,
                     envoy::service::ext_proc::v3::ProcessingRequest& request) override {
    // Do something simple to mark that we were here
    if (request.has_request_headers()) {
      request.mutable_request_headers()->mutable_headers()->add_headers()->set_key(
          "x-test-request-modifier");
      return true;
    }
    return false;
  }
};

class TestProcessingRequestModifierFactory : public ProcessingRequestModifierFactory {
public:
  TestProcessingRequestModifierFactory() = default;

  std::unique_ptr<ProcessingRequestModifier> createProcessingRequestModifier(
      const Protobuf::Message&, Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr,
      Envoy::Server::Configuration::CommonFactoryContext&) const override {
    return std::make_unique<TestProcessingRequestModifier>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::Protobuf::Struct()};
  }

  std::string name() const override { return "test_processing_request_modifier"; }
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
