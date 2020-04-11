#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/decompressor/decompressor.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Decompressor {

class DecompressorFactory {
public:
  virtual ~DecompressorFactory() = default;

  virtual DecompressorPtr createDecompressor() PURE;
  virtual const std::string& statsPrefix() const PURE;
  // TODO(junr03): this method assumes that decompressors are used on http messages.
  // A more generic method might be `hint()` which gives the user of the decompressor a hint about
  // the type of decompression that it can perform.
  virtual const std::string& contentEncoding() const PURE;
};

using DecompressorFactoryPtr = std::unique_ptr<DecompressorFactory>;

class NamedDecompressorLibraryConfigFactory : public Config::TypedFactory {
public:
  ~NamedDecompressorLibraryConfigFactory() override = default;

  virtual DecompressorFactoryPtr
  createDecompressorLibraryFromProto(const Protobuf::Message& config,
                                     Server::Configuration::FactoryContext& context) PURE;
};

} // namespace Decompressor
} // namespace Envoy