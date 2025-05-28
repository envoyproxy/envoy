#pragma once

#include "envoy/server/bootstrap_extension_config.h"

#include "source/extensions/filters/common/expr/evaluator.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace Cel {

class CelBootstrapExtension : public Server::BootstrapExtension {
public:
  CelBootstrapExtension(const envoy::extensions::filters::common::expr::v3::CelEvaluatorConfig& config)
      : config_(config) {}

  void onServerInitialized() override {}
  void onWorkerThreadInitialized() override {}

private:
  const envoy::extensions::filters::common::expr::v3::CelEvaluatorConfig config_;
};

class CelFactory : public Server::Configuration::BootstrapExtensionFactory {
public:
  Server::BootstrapExtensionPtr createBootstrapExtension(
      const Protobuf::Message& config,
      Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::common::expr::v3::CelEvaluatorConfig>();
  }

  std::string name() const override { return "envoy.bootstrap.cel"; }
};

} // namespace Cel
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy 