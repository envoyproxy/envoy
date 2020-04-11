#pragma once

#include "envoy/decompressor/config.h"
#include "envoy/decompressor/decompressor.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace Decompressors {

template <class ConfigProto>
class DecompressorLibraryFactoryBase : public Decompressor::NamedDecompressorLibraryConfigFactory {
public:
  Decompressor::DecompressorFactoryPtr
  createDecompressorLibraryFromProto(const Protobuf::Message& proto_config,
                                     Server::Configuration::FactoryContext& context) override {
    return createDecompressorLibraryFromProtoTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(proto_config,
                                                             context.messageValidationVisitor()));
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }
  std::string category() const override { return "envoy.decompressors"; }
  std::string name() const override { return name_; }

protected:
  DecompressorLibraryFactoryBase(const std::string& name) : name_(name) {}

private:
  virtual Decompressor::DecompressorFactoryPtr
  createDecompressorLibraryFromProtoTyped(const ConfigProto&) PURE;
  const std::string name_;
};

} // namespace Decompressors
} // namespace Extensions
} // namespace Envoy
