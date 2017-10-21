#pragma once

#include "envoy/network/connection.h"
#include "envoy/server/filter_config.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class NamedTransportSecurityConfigFactory {
 public:
  virtual ~NamedTransportSecurityConfigFactory() {}

  virtual Network::TransportSecurityFactoryCb createClientTransportSecurityFactory(
      const Protobuf::Message& config,
      FactoryContext& context) PURE;
  virtual Network::TransportSecurityFactoryCb createServerTransportSecurityFactory(
      const Protobuf::Message& config,
      FactoryContext& context) PURE;

  virtual ProtobufTypes::MessagePtr createEmptyClientConfigProto() PURE;
  virtual ProtobufTypes::MessagePtr createEmptyServerConfigProto() PURE;
  virtual std::string name() PURE;
};

}
}
}