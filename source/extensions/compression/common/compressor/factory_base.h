#pragma once

#include "envoy/compression/compressor/config.h"
#include "envoy/compression/compressor/factory.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Common {
namespace Compressor {

template <class ConfigProto>
class CompressorLibraryFactoryBase
    : public Envoy::Compression::Compressor::NamedCompressorLibraryConfigFactory {
public:
  Envoy::Compression::Compressor::CompressorFactoryPtr
  createCompressorFactoryFromProto(const Protobuf::Message& proto_config,
                                   Server::Configuration::FactoryContext& context) override {
    return createCompressorFactoryFromProtoTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(proto_config,
                                                             context.messageValidationVisitor()));
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return name_; }

protected:
  CompressorLibraryFactoryBase(const std::string& name) : name_(name) {}

private:
  virtual Envoy::Compression::Compressor::CompressorFactoryPtr
  createCompressorFactoryFromProtoTyped(const ConfigProto&) PURE;

  const std::string name_;
};

} // namespace Compressor
} // namespace Common
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
