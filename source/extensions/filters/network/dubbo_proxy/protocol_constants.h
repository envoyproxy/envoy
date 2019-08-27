#pragma once

#include <unordered_map>

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/singleton/const_singleton.h"

#include "extensions/filters/network/dubbo_proxy/message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

/**
 * Names of available Protocol implementations.
 */
class ProtocolNameValues {
public:
  struct ProtocolTypeHash {
    template <typename T> std::size_t operator()(T t) const { return static_cast<std::size_t>(t); }
  };

  using ProtocolTypeNameMap = std::unordered_map<ProtocolType, std::string, ProtocolTypeHash>;

  const ProtocolTypeNameMap protocolTypeNameMap = {
      {ProtocolType::Dubbo, "dubbo"},
  };

  const std::string& fromType(ProtocolType type) const {
    const auto& itor = protocolTypeNameMap.find(type);
    ASSERT(itor != protocolTypeNameMap.end());
    return itor->second;
  }
};

using ProtocolNames = ConstSingleton<ProtocolNameValues>;

/**
 * Names of available serializer implementations.
 */
class SerializerNameValues {
public:
  struct SerializationTypeHash {
    template <typename T> std::size_t operator()(T t) const { return static_cast<std::size_t>(t); }
  };

  using SerializerTypeNameMap =
      std::unordered_map<SerializationType, std::string, SerializationTypeHash>;

  const SerializerTypeNameMap serializerTypeNameMap = {
      {SerializationType::Hessian2, "hessian2"},
  };

  const std::string& fromType(SerializationType type) const {
    const auto& itor = serializerTypeNameMap.find(type);
    ASSERT(itor != serializerTypeNameMap.end());
    return itor->second;
  }
};

using SerializerNames = ConstSingleton<SerializerNameValues>;

class ProtocolSerializerNameValues {
public:
  inline uint8_t generateKey(ProtocolType protocol_type,
                             SerializationType serialization_type) const {
    return static_cast<uint8_t>(serialization_type) ^ static_cast<uint8_t>(protocol_type);
  }

  inline std::string generateValue(ProtocolType protocol_type,
                                   SerializationType serialization_type) const {
    return fmt::format("{}.{}", ProtocolNames::get().fromType(protocol_type),
                       SerializerNames::get().fromType(serialization_type));
  }

#define GENERATE_PAIR(X, Y) generateKey(X, Y), generateValue(X, Y)

  using ProtocolSerializerTypeNameMap = std::unordered_map<uint8_t, std::string>;

  const ProtocolSerializerTypeNameMap protocolSerializerTypeNameMap = {
      {GENERATE_PAIR(ProtocolType::Dubbo, SerializationType::Hessian2)},
  };

  const std::string& fromType(ProtocolType protocol_type, SerializationType type) const {
    const auto& itor = protocolSerializerTypeNameMap.find(generateKey(protocol_type, type));
    ASSERT(itor != protocolSerializerTypeNameMap.end());
    return itor->second;
  }
};

using ProtocolSerializerNames = ConstSingleton<ProtocolSerializerNameValues>;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
