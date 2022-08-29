#pragma once

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace ProtoTransformer {

/*
 * A transformer transforms a protobuf to another. The output is a generic proto message.
 */
template <class ProtoType> class ProtoTransformer {
public:
  virtual ~ProtoTransformer() = default;

  virtual std::unique_ptr<Protobuf::Message> transform(const ProtoType& proto) const PURE;
};

/*
 * A factory for creating ProtoTransformer.
 */
template <class ProtoType> class ProtoTransformerFactory : public Envoy::Config::TypedFactory {
public:
  ~ProtoTransformerFactory() override = default;

  virtual std::unique_ptr<ProtoTransformer<ProtoType>>
  createProtoTransformer(const Protobuf::Message& config) PURE;

  std::string category() const override { return "envoy.proto_transformer"; };

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Envoy::ProtobufWkt::Struct>();
  }
};

} // namespace ProtoTransformer
} // namespace Envoy
