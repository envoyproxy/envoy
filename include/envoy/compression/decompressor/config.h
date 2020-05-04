#pragma once

#include "envoy/compression/decompressor/factory.h"
#include "envoy/config/typed_config.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Compression {
namespace Decompressor {

class NamedDecompressorLibraryConfigFactory : public Config::TypedFactory {
public:
  ~NamedDecompressorLibraryConfigFactory() override = default;

  virtual DecompressorFactoryPtr
  createDecompressorFactoryFromProto(const Protobuf::Message& config,
                                     Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "envoy.compression.decompressor"; }
};

} // namespace Decompressor
} // namespace Compression
} // namespace Envoy