#pragma once

#include "envoy/common/pure.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

/**
 * Base class for an extension factory.
 */
class UntypedFactory {
public:
  virtual ~UntypedFactory() = default;

  /**
   * Name of the factory, a reversed DNS name is encouraged to avoid cross-org conflict.
   * It's used as key in the metadata map, as well as key in the factory registry.
   */
  virtual std::string name() const PURE;

  /**
   * @return std::string the identifying category name for objects
   * created by this factory. Used for automatic registration with
   * FactoryCategoryRegistry.
   */
  virtual std::string category() const PURE;

  /**
   * @return all full names of configuration protos that used by the factory. Empty set
   * will be returned for untyped factories.
   */
  virtual std::set<std::string> configTypes() { return {}; }
};

/**
 * Base class for an extension factory configured by a typed proto message.
 */
class TypedFactory : public UntypedFactory {
public:
  ~TypedFactory() override = default;

  /**
   * @return ProtobufTypes::MessagePtr create empty config proto message for v2. The config, which
   * arrives in an opaque google.protobuf.Struct message, will be converted to JSON and then parsed
   * into this empty proto.
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;

  std::set<std::string> configTypes() override {
    auto ptr = createEmptyConfigProto();
    ASSERT(ptr != nullptr);
    Protobuf::ReflectableMessage reflectable_message = createReflectableMessage(*ptr);
    return {reflectable_message->GetDescriptor()->full_name()};
  }
};

} // namespace Config
} // namespace Envoy
