#pragma once

#include <string>

#include "envoy/compression/compressor/factory.h"
#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Compression {
namespace Compressor {

class NamedCompressorLibraryConfigFactory : public Config::TypedFactory {
public:
  ~NamedCompressorLibraryConfigFactory() override = default;

  virtual CompressorFactoryPtr
  createCompressorFactoryFromProto(const Protobuf::Message& config,
                                   Server::Configuration::GenericFactoryContext& context) PURE;

  std::string category() const override { return "envoy.compression.compressor"; }
};

} // namespace Compressor
} // namespace Compression
} // namespace Envoy
