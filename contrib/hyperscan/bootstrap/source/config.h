#pragma once

#include "envoy/thread_local/thread_local.h"

#include "source/common/common/regex_engine.h"

namespace Envoy {
namespace Extensions {
namespace Regex {
namespace Hyperscan {

class HyperscanEngine : public Envoy::Regex::EngineBase {
public:
  Envoy::Regex::CompiledMatcherPtr matcher(const std::string& regex) const override;

  // Server::Configuration::BootstrapExtensionFactory
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override { return "envoy.bootstrap.hyperscan"; };

private:
  unsigned int flag_{};
  OptRef<ThreadLocal::SlotAllocator> tls_{};
};

DECLARE_FACTORY(HyperscanEngine);

} // namespace Hyperscan
} // namespace Regex
} // namespace Extensions
} // namespace Envoy
