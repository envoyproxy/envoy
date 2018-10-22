#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

/**
 * TypedMetadata interface.
 */
class TypedMetadata {
public:
  class Object {
  public:
    virtual ~Object() {}
  };

  virtual ~TypedMetadata() {}

  // Returns a T instance by key. If the conversion is not able to complete, or
  // if the data is not in the store, returns a nullptr.
  template <typename T> const T* get(const std::string& key) const {
    static_assert(std::is_base_of<Object, T>::value,
                  "Data type must be subclass of TypedMetadata::Object");
    const Object* p = getData(key);
    if (p != nullptr) {
      return dynamic_cast<const T*>(p);
    }
    return nullptr;
  }

protected:
  // Returns data associated with given 'key'.
  // If there is no data associated with this key, a nullptr is returned.
  virtual const Object* getData(const std::string& key) const PURE;
};

/*
 * Typed metadata should implement this factory and register via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class TypedMetadataFactory {
public:
  virtual ~TypedMetadataFactory() {}

  // Name of the factory.
  // It's used as key in the metadata map, as well as key in the factory registry.
  // When building a TypedMetadata from envoy::api::v2::core::Metadata, if the key is not found, the
  // parse will not be called and the corresponding typedMetadata entry will be set to nullptr.
  virtual const std::string name() const PURE;

  // Convert the google.protobuf.Struct into an instance of TypedMetadata::Object.
  // It should throw an EnvoyException in case the conversion can't be completed.
  // Returns a derived class object pointer of TypedMetadata.
  virtual std::unique_ptr<const TypedMetadata::Object> parse(const ProtobufWkt::Struct&) const PURE;
};

} // namespace Config
} // namespace Envoy
