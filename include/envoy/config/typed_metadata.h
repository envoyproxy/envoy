#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"

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
    virtual ~Object() = default;
  };

  virtual ~TypedMetadata() = default;

  /**
   * @return a T instance by key. If the conversion is not able to complete, or
   * if the data is not in the store, returns a nullptr.
   */
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
  /**
   * Returns data associated with given 'key'.
   * If there is no data associated with this key, a nullptr is returned.
   * @param key the key (usually a reversed DNS) associated with the typed metadata.
   * @return A TypedMetadata::Object pointer, nullptr if no data is associated with the key.
   */
  virtual const Object* getData(const std::string& key) const PURE;
};

/**
 * Typed metadata should implement this factory and register via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class TypedMetadataFactory : public UntypedFactory {
public:
  ~TypedMetadataFactory() override = default;

  /**
   * Convert the google.protobuf.Struct into an instance of TypedMetadata::Object.
   * It should throw an EnvoyException in case the conversion can't be completed.
   * @param data config data stored as a protobuf struct.
   * @return a derived class object pointer of TypedMetadata.
   * @throw EnvoyException if the parsing can't be done.
   */
  virtual std::unique_ptr<const TypedMetadata::Object>
  parse(const ProtobufWkt::Struct& data) const PURE;

  std::string category() const override { return "envoy.typed_metadata"; }
};

} // namespace Config
} // namespace Envoy
