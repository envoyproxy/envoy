#pragma once

#include "envoy/server/bootstrap_extension_config.h"

#include "source/extensions/bootstrap/cel/cel.pb.h"
#include "source/extensions/filters/common/expr/evaluator.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace Cel {

class CelBootstrapExtension : public Server::BootstrapExtension {
public:
  CelBootstrapExtension(const envoy::extensions::bootstrap::cel::CelEvaluatorConfig& config)
      : config_(config) {}

  void onServerInitialized() override {}
  void onWorkerThreadInitialized() override {}

  const envoy::extensions::bootstrap::cel::CelEvaluatorConfig& config() const { return config_; }

private:
  const envoy::extensions::bootstrap::cel::CelEvaluatorConfig config_;
};

class CelFactory : public Server::Configuration::BootstrapExtensionFactory {
public:
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::bootstrap::cel::CelEvaluatorConfig>();
  }

  std::string name() const override { return "envoy.bootstrap.cel"; }
};

} // namespace Cel
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
