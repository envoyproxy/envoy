#pragma once

#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "udpa/type/v1/typed_struct.pb.h"
#include "xds/type/v3/typed_struct.pb.h"

namespace Envoy {
namespace Config {
class RegistryUtils {
public:
  /**
   * Get a Factory from the registry with a particular name (and templated type) with error checking
   * to ensure the name and factory are valid.
   * @param name string identifier for the particular implementation.
   * @param is_optional exception will be throw when the value is false and no factory found.
   * @return factory the factory requested or nullptr if it does not exist.
   */
  template <class Factory>
  static Factory* getAndCheckFactoryByName(const std::string& name, bool is_optional) {
    if (name.empty()) {
      ExceptionUtil::throwEnvoyException("Provided name for static registration lookup was empty.");
    }

    Factory* factory = Registry::FactoryRegistry<Factory>::getFactory(name);

    if (factory == nullptr && !is_optional) {
      ExceptionUtil::throwEnvoyException(
          fmt::format("Didn't find a registered implementation for name: '{}'", name));
    }

    return factory;
  }

  /**
   * Get a Factory from the registry with a particular name (and templated type) with error checking
   * to ensure the name and factory are valid.
   * @param name string identifier for the particular implementation.
   * @return factory the factory requested or nullptr if it does not exist.
   */
  template <class Factory> static Factory& getAndCheckFactoryByName(const std::string& name) {
    return *getAndCheckFactoryByName<Factory>(name, false);
  }

  /**
   * Get a Factory from the registry with a particular name or return nullptr.
   * @param name string identifier for the particular implementation.
   */
  template <class Factory> static Factory* getFactoryByName(const std::string& name) {
    if (name.empty()) {
      return nullptr;
    }

    return Registry::FactoryRegistry<Factory>::getFactory(name);
  }

  /**
   * Get a Factory from the registry or return nullptr.
   * @param message proto that contains fields 'name' and 'typed_config'.
   */
  template <class Factory, class ProtoMessage>
  static Factory* getFactory(const ProtoMessage& message) {
    Factory* factory = getFactoryByType<Factory>(message.typed_config());
    if (factory != nullptr) {
      return factory;
    }

    return getFactoryByName<Factory>(message.name());
  }

  /**
   * Get a Factory from the registry with error checking to ensure the name and the factory are
   * valid. And a flag to control return nullptr or throw an exception.
   * @param message proto that contains fields 'name' and 'typed_config'.
   * @param is_optional an exception will be throw when the value is false and no factory found.
   * @return factory the factory requested or nullptr if it does not exist.
   */
  template <class Factory, class ProtoMessage>
  static Factory* getAndCheckFactory(const ProtoMessage& message, bool is_optional) {
    Factory* factory = getFactoryByType<Factory>(message.typed_config());
    if (factory != nullptr) {
      return factory;
    }

    return getAndCheckFactoryByName<Factory>(message.name(), is_optional);
  }

  /**
   * Get a Factory from the registry with error checking to ensure the name and the factory are
   * valid.
   * @param message proto that contains fields 'name' and 'typed_config'.
   */
  template <class Factory, class ProtoMessage>
  static Factory& getAndCheckFactory(const ProtoMessage& message) {
    return *getAndCheckFactory<Factory>(message, false);
  }

  /**
   * Get type URL from a typed config.
   * @param typed_config for the extension config.
   */
  static std::string getFactoryType(const ProtobufWkt::Any& typed_config) {
    static const std::string& typed_struct_type =
        xds::type::v3::TypedStruct::default_instance().GetDescriptor()->full_name();
    static const std::string& legacy_typed_struct_type =
        udpa::type::v1::TypedStruct::default_instance().GetDescriptor()->full_name();
    // Unpack methods will only use the fully qualified type name after the last '/'.
    // https://github.com/protocolbuffers/protobuf/blob/3.6.x/src/google/protobuf/any.proto#L87
    auto type = std::string(TypeUtil::typeUrlToDescriptorFullName(typed_config.type_url()));
    if (type == typed_struct_type) {
      xds::type::v3::TypedStruct typed_struct;
      MessageUtil::unpackTo(typed_config, typed_struct);
      // Not handling nested structs or typed structs in typed structs
      return std::string(TypeUtil::typeUrlToDescriptorFullName(typed_struct.type_url()));
    } else if (type == legacy_typed_struct_type) {
      udpa::type::v1::TypedStruct typed_struct;
      MessageUtil::unpackTo(typed_config, typed_struct);
      // Not handling nested structs or typed structs in typed structs
      return std::string(TypeUtil::typeUrlToDescriptorFullName(typed_struct.type_url()));
    }
    return type;
  }

  /**
   * Get a Factory from the registry by type URL.
   * @param typed_config for the extension config.
   */
  template <class Factory> static Factory* getFactoryByType(const ProtobufWkt::Any& typed_config) {
    if (typed_config.type_url().empty()) {
      return nullptr;
    }
    return Registry::FactoryRegistry<Factory>::getFactoryByType(getFactoryType(typed_config));
  }

  /**
   * Translate a nested config into a proto message provided by the implementation factory.
   * @param enclosing_message proto that contains a field 'typed_config'. Note: the enclosing proto
   * is provided because for statically registered implementations, a custom config is generally
   * optional, which means the conversion must be done conditionally.
   * @param validation_visitor message validation visitor instance.
   * @param factory implementation factory with the method 'createEmptyConfigProto' to produce a
   * proto to be filled with the translated configuration.
   */
  template <class ProtoMessage, class Factory>
  static ProtobufTypes::MessagePtr
  translateToFactoryConfig(const ProtoMessage& enclosing_message,
                           ProtobufMessage::ValidationVisitor& validation_visitor,
                           Factory& factory) {
    ProtobufTypes::MessagePtr config = factory.createEmptyConfigProto();

    // Fail in an obvious way if a plugin does not return a proto.
    RELEASE_ASSERT(config != nullptr, "");

    // Check that the config type is not google.protobuf.Empty
    RELEASE_ASSERT(config->GetDescriptor()->full_name() != "google.protobuf.Empty", "");

    translateOpaqueConfig(enclosing_message.typed_config(), validation_visitor, *config);
    return config;
  }

  /**
   * Translate the typed any field into a proto message provided by the implementation factory.
   * @param typed_config typed configuration.
   * @param validation_visitor message validation visitor instance.
   * @param factory implementation factory with the method 'createEmptyConfigProto' to produce a
   * proto to be filled with the translated configuration.
   */
  template <class Factory>
  static ProtobufTypes::MessagePtr
  translateAnyToFactoryConfig(const ProtobufWkt::Any& typed_config,
                              ProtobufMessage::ValidationVisitor& validation_visitor,
                              Factory& factory) {
    ProtobufTypes::MessagePtr config = factory.createEmptyConfigProto();

    // Fail in an obvious way if a plugin does not return a proto.
    RELEASE_ASSERT(config != nullptr, "");

    // Check that the config type is not google.protobuf.Empty
    RELEASE_ASSERT(config->GetDescriptor()->full_name() != "google.protobuf.Empty", "");

    translateOpaqueConfig(typed_config, validation_visitor, *config);
    return config;
  }

  /**
   * Translate opaque config from google.protobuf.Any to defined proto message.
   * @param typed_config opaque config packed in google.protobuf.Any
   * @param validation_visitor message validation visitor instance.
   * @param out_proto the proto message instantiated by extensions
   */
  static void translateOpaqueConfig(const ProtobufWkt::Any& typed_config,
                                    ProtobufMessage::ValidationVisitor& validation_visitor,
                                    Protobuf::Message& out_proto);
};
} // namespace Config
} // namespace Envoy
