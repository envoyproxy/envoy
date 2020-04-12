#pragma once

#include "envoy/compression/decompressor/config.h"
#include "envoy/compression/decompressor/decompressor.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Common {
namespace Decompressor {

template <class ConfigProto>
class DecompressorLibraryFactoryBase
    : public Envoy::Compression::Decompressor::NamedDecompressorLibraryConfigFactory {
public:
  Envoy::Compression::Decompressor::DecompressorFactoryPtr
  createDecompressorLibraryFromProto(const Protobuf::Message& proto_config,
                                     Server::Configuration::FactoryContext& context) override {
    return createDecompressorLibraryFromProtoTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(proto_config,
                                                             context.messageValidationVisitor()));
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }
  std::string category() const override { return "envoy.compression.decompressors"; }
  std::string name() const override { return name_; }

protected:
  DecompressorLibraryFactoryBase(const std::string& name) : name_(name) {}

private:
  virtual Envoy::Compression::Decompressor::DecompressorFactoryPtr
  createDecompressorLibraryFromProtoTyped(const ConfigProto&) PURE;
  const std::string name_;
};

} // namespace Decompressor
} // namespace Common
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
