#pragma once

#include "envoy/extensions/http/early_header_mutation/exact_path_rewrite/v3/exact_path_rewrite.pb.h"
#include "envoy/extensions/http/early_header_mutation/exact_path_rewrite/v3/exact_path_rewrite.pb.validate.h"
#include "envoy/http/early_header_mutation.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace ExactPathRewrite {

using ProtoExactPathRewrite =
    envoy::extensions::http::early_header_mutation::exact_path_rewrite::v3::ExactPathRewrite;

class Factory : public Envoy::Http::EarlyHeaderMutationFactory {
public:
  std::string name() const override {
    return "envoy.http.early_header_mutation.exact_path_rewrite";
  }

  Envoy::Http::EarlyHeaderMutationPtr
  createExtension(const Protobuf::Message& config,
                  Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtoExactPathRewrite>();
  }
};

} // namespace ExactPathRewrite
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
