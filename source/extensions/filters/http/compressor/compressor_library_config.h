#pragma once

#include "envoy/config/typed_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {

class NamedCompressorLibraryConfigFactory : public Config::TypedFactory {
public:
  ~NamedCompressorLibraryConfigFactory() override = default;

  virtual CompressorFactoryPtr
  createCompressorLibraryFromProto(const Protobuf::Message& config,
                                   Server::Configuration::FactoryContext& context) PURE;
};

} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
