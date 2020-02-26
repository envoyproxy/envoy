#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/event/dispatcher.h"
#include "envoy/registry/registry.h"
#include "envoy/singleton/manager.h"
#include "envoy/type/metadata/v3/metadata.pb.h"

#include "common/protobuf/protobuf.h"
#include "common/shared_pool/shared_pool.h"

namespace Envoy {
namespace Config {

using ConstMetadataSharedPoolSharedPtr = std::shared_ptr<
    SharedPool::ObjectSharedPool<const envoy::config::core::v3::Metadata, MessageUtil>>;

/**
 * MetadataKey presents the key name and path to retrieve value from metadata.
 */
struct MetadataKey {
  std::string key_;
  std::vector<std::string> path_;

  MetadataKey(const envoy::type::metadata::v3::MetadataKey& metadata_key);
};

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
  static const ProtobufWkt::Value& metadataValue(const envoy::config::core::v3::Metadata* metadata,
                                                 const std::string& filter, const std::string& key);
  /**
   * Lookup value by a multi-key path for a given filter in Metadata. If path is empty
   * will return the empty struct.
   * @param metadata reference.
   * @param filter name.
   * @param path multi-key path.
   * @return const ProtobufWkt::Value& value if found, empty if not found.
   */
  static const ProtobufWkt::Value& metadataValue(const envoy::config::core::v3::Metadata* metadata,
                                                 const std::string& filter,
                                                 const std::vector<std::string>& path);
  /**
   * Lookup the value by a metadata key from a Metadata.
   * @param metadata reference.
   * @param metadata_key with key name and path to retrieve the value.
   * @return const ProtobufWkt::Value& value if found, empty if not found.
   */
  static const ProtobufWkt::Value& metadataValue(const envoy::config::core::v3::Metadata* metadata,
                                                 const MetadataKey& metadata_key);

  /**
   * Obtain mutable reference to metadata value for a given filter and key.
   * @param metadata reference.
   * @param filter name.
   * @param key for filter metadata.
   * @return ProtobufWkt::Value&. A Value message is created if not found.
   */
  static ProtobufWkt::Value& mutableMetadataValue(envoy::config::core::v3::Metadata& metadata,
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
                                 const envoy::config::core::v3::Metadata* host_metadata,
                                 const std::string& filter_key, bool list_as_any);
  /**
   * Returns an ObjectSharedPool to store const Metadata
   * @param manager used to create singleton
   * @param dispatcher the dispatcher object reference to the thread that created the
   * ObjectSharedPool
   */
  static ConstMetadataSharedPoolSharedPtr getConstMetadataSharedPool(Singleton::Manager& manager,
                                                                     Event::Dispatcher& dispatcher);
};

template <typename factoryClass> class TypedMetadataImpl : public TypedMetadata {
public:
  static_assert(std::is_base_of<Config::TypedMetadataFactory, factoryClass>::value,
                "Factory type must be inherited from Envoy::Config::TypedMetadataFactory.");
  TypedMetadataImpl(const envoy::config::core::v3::Metadata& metadata) { populateFrom(metadata); }

  const TypedMetadata::Object* getData(const std::string& key) const override {
    const auto& it = data_.find(key);
    return it == data_.end() ? nullptr : it->second.get();
  }

protected:
  /* Attempt to run each of the registered factories for TypedMetadata, to
   * populate the data_ map.
   */
  void populateFrom(const envoy::config::core::v3::Metadata& metadata) {
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
