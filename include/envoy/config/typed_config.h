#pragma once

#include "envoy/common/pure.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

/**
 * Base class for config defined by a typed proto message.
 */
class TypedConfig {
public:
  /**
   * @return ProtobufTypes::MessagePtr create empty config proto message for v2. The config, which
   * arrives in an opaque google.protobuf.Struct message, will be converted to JSON and then parsed
   * into this empty proto.
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;

  /**
   * @return config proto full name
   */
  virtual std::string type() {
    auto ptr = createEmptyConfigProto();
    if (ptr == nullptr) {
      return "";
    }
    return ptr->GetDescriptor()->full_name();
  }

  virtual ~TypedConfig() = default;
};

} // namespace Config
} // namespace Envoy
