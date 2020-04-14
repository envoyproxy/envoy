#pragma once

#include "envoy/server/filter_config.h"

#include "extensions/filters/http/compressor/compressor_factory.h"
#include "extensions/filters/http/compressor/compressor_library_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {

template <class ConfigProto>
class CompressorLibraryFactoryBase : public NamedCompressorLibraryConfigFactory {
public:
  CompressorFactoryPtr
  createCompressorFactoryFromProto(const Protobuf::Message& proto_config,
                                   Server::Configuration::FactoryContext& context) override {
    return createCompressorFactoryFromProtoTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(proto_config,
                                                             context.messageValidationVisitor()));
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string category() const override { return "envoy.filters.http.compressor"; }

  std::string name() const override { return name_; }

protected:
  CompressorLibraryFactoryBase(const std::string& name) : name_(name) {}

private:
  virtual CompressorFactoryPtr createCompressorFactoryFromProtoTyped(const ConfigProto&) PURE;
  const std::string name_;
};

} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
