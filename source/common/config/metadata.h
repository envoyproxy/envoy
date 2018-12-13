#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/registry/registry.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

/**
 * Config metadata helpers.
 */
class Metadata {
public:
  /**
   * Lookup value of a key for a given filter in Metadata.
   * @param metadata reference.
   * @param filter name.
   * @param key for filter metadata.
   * @return const ProtobufWkt::Value& value if found, empty if not found.
   */
  static const ProtobufWkt::Value& metadataValue(const envoy::api::v2::core::Metadata& metadata,
                                                 const std::string& filter, const std::string& key);
  /**
   * Lookup value by a multi-key path for a given filter in Metadata. If path is empty
   * will return the empty struct.
   * @param metadata reference.
   * @param filter name.
   * @param path multi-key path.
   * @return const ProtobufWkt::Value& value if found, empty if not found.
   */
  static const ProtobufWkt::Value& metadataValue(const envoy::api::v2::core::Metadata& metadata,
                                                 const std::string& filter,
                                                 const std::vector<std::string>& path);
  /**
   * Obtain mutable reference to metadata value for a given filter and key.
   * @param metadata reference.
   * @param filter name.
   * @param key for filter metadata.
   * @return ProtobufWkt::Value&. A Value message is created if not found.
   */
  static ProtobufWkt::Value& mutableMetadataValue(envoy::api::v2::core::Metadata& metadata,
                                                  const std::string& filter,
                                                  const std::string& key);
};

template <typename factoryClass> class TypedMetadataImpl : public TypedMetadata {
public:
  static_assert(std::is_base_of<Config::TypedMetadataFactory, factoryClass>::value,
                "Factory type must be inherited from Envoy::Config::TypedMetadataFactory.");
  TypedMetadataImpl(const envoy::api::v2::core::Metadata& metadata) : data_() {
    auto& data_by_key = metadata.filter_metadata();
    for (const auto& it : Registry::FactoryRegistry<factoryClass>::factories()) {
      const auto& meta_iter = data_by_key.find(it.first);
      if (meta_iter != data_by_key.end()) {
        data_[it.second->name()] = it.second->parse(meta_iter->second);
      }
    }
  }

  const TypedMetadata::Object* getData(const std::string& key) const override {
    const auto& it = data_.find(key);
    return it == data_.end() ? nullptr : it->second.get();
  }

private:
  std::unordered_map<std::string, std::unique_ptr<const TypedMetadata::Object>> data_;
};

} // namespace Config
} // namespace Envoy
