#pragma once

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace ProtoJson {

/*
 * A marshaller for marshaling protobuf into JSON format.
 */
template <class ProtoType> class ProtoJsonMarshaller {
public:
  virtual ~ProtoJsonMarshaller() = default;

  // Outputs the protobuf into JSON format.
  virtual Protobuf::util::Status marshal(const ProtoType& proto, std::string& output) const PURE;
};

/*
 * A factory for creating ProtoJsonMarshaller.
 */
template <class ProtoType> class ProtoJsonMarshallerFactory : public Envoy::Config::TypedFactory {
public:
  ~ProtoJsonMarshallerFactory() override = default;

  virtual std::unique_ptr<ProtoJsonMarshaller<ProtoType>>
  createProtoJsonMarshaller(const Protobuf::Message& config) PURE;

  std::string category() const override { return "envoy.proto_json_marshaller"; };

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Envoy::ProtobufWkt::Struct>();
  }
};

} // namespace ProtoJson
} // namespace Envoy
