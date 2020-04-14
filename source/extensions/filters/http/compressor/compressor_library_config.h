#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/compressor/compressor_factory.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {

class NamedCompressorLibraryConfigFactory : public Config::TypedFactory {
public:
  ~NamedCompressorLibraryConfigFactory() override = default;

  virtual CompressorFactoryPtr
  createCompressorFactoryFromProto(const Protobuf::Message& config,
                                   Server::Configuration::FactoryContext& context) PURE;
};

} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
