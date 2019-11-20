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

  using LabelSet = std::vector<std::pair<std::string, ProtobufWkt::Value>>;

  /**
   * Returns whether a set of the labels match a particular host's metadata.
   * @param label_set the target label key/value pair set.
   * @param host_metadata a given host's metadata.
   * @param filter_key identifies the entry in the metadata entry for the match.
   * @param list_as_any if the metadata value entry is a list, and any one of
   * the element equals to the input label_set, it's considered as match.
   */
  static bool metadataLabelMatch(const LabelSet& label_set,
                                 const envoy::api::v2::core::Metadata& host_metadata,
                                 const std::string& filter_key, bool list_as_any);
};

template <typename factoryClass> class TypedMetadataImpl : public TypedMetadata {
public:
  static_assert(std::is_base_of<Config::TypedMetadataFactory, factoryClass>::value,
                "Factory type must be inherited from Envoy::Config::TypedMetadataFactory.");
  TypedMetadataImpl(const envoy::api::v2::core::Metadata& metadata) { populateFrom(metadata); }

  const TypedMetadata::Object* getData(const std::string& key) const override {
    const auto& it = data_.find(key);
    return it == data_.end() ? nullptr : it->second.get();
  }

protected:
  /* Attempt to run each of the registered factories for TypedMetadata, to
   * populate the data_ map.
   */
  void populateFrom(const envoy::api::v2::core::Metadata& metadata) {
    auto& data_by_key = metadata.filter_metadata();
    for (const auto& it : Registry::FactoryRegistry<factoryClass>::factories()) {
      const auto& meta_iter = data_by_key.find(it.first);
      if (meta_iter != data_by_key.end()) {
        data_[it.second->name()] = it.second->parse(meta_iter->second);
      }
    }
  }

  std::unordered_map<std::string, std::unique_ptr<const TypedMetadata::Object>> data_;
};

} // namespace Config
} // namespace Envoy
