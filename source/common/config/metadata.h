#pragma once

#include <memory>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/event/dispatcher.h"
#include "envoy/registry/registry.h"
#include "envoy/singleton/manager.h"
#include "envoy/type/metadata/v3/metadata.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/shared_pool/shared_pool.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Config {

using ConstMetadataSharedPoolSharedPtr =
    std::shared_ptr<SharedPool::ObjectSharedPool<const envoy::config::core::v3::Metadata,
                                                 MessageUtil, MessageUtil>>;

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
   * Lookup value by a multi-key path in a Struct. If path is empty will return the entire struct.
   * @param struct_value reference.
   * @param path multi-key path.
   * @return const Protobuf::Value& value if found, empty if not found.
   */
  static const Protobuf::Value& structValue(const Protobuf::Struct& struct_value,
                                            const std::vector<std::string>& path);

  /**
   * Lookup value of a key for a given filter in Metadata.
   * @param metadata reference.
   * @param filter name.
   * @param key for filter metadata.
   * @return const Protobuf::Value& value if found, empty if not found.
   */
  static const Protobuf::Value& metadataValue(const envoy::config::core::v3::Metadata* metadata,
                                              const std::string& filter, const std::string& key);
  /**
   * Lookup value by a multi-key path for a given filter in Metadata. If path is empty
   * will return the empty struct.
   * @param metadata reference.
   * @param filter name.
   * @param path multi-key path.
   * @return const Protobuf::Value& value if found, empty if not found.
   */
  static const Protobuf::Value& metadataValue(const envoy::config::core::v3::Metadata* metadata,
                                              const std::string& filter,
                                              const std::vector<std::string>& path);
  /**
   * Lookup the value by a metadata key from a Metadata.
   * @param metadata reference.
   * @param metadata_key with key name and path to retrieve the value.
   * @return const Protobuf::Value& value if found, empty if not found.
   */
  static const Protobuf::Value& metadataValue(const envoy::config::core::v3::Metadata* metadata,
                                              const MetadataKey& metadata_key);

  /**
   * Obtain mutable reference to metadata value for a given filter and key.
   * @param metadata reference.
   * @param filter name.
   * @param key for filter metadata.
   * @return Protobuf::Value&. A Value message is created if not found.
   */
  static Protobuf::Value& mutableMetadataValue(envoy::config::core::v3::Metadata& metadata,
                                               const std::string& filter, const std::string& key);

  using LabelSet = std::vector<std::pair<std::string, Protobuf::Value>>;

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
    auto& typed_data_by_key = metadata.typed_filter_metadata();
    for (const auto& [factory_name, factory] :
         Registry::FactoryRegistry<factoryClass>::factories()) {
      const auto& typed_meta_iter = typed_data_by_key.find(factory_name);
      // If the key exists in Any metadata, and parse() does not return nullptr,
      // populate data_.
      if (typed_meta_iter != typed_data_by_key.end()) {
        auto result = factory->parse(typed_meta_iter->second);
        if (result != nullptr) {
          data_[factory->name()] = std::move(result);
          continue;
        }
      }
      // Fall back cases to parsing Struct metadata and populate data_.
      const auto& meta_iter = data_by_key.find(factory_name);
      if (meta_iter != data_by_key.end()) {
        data_[factory->name()] = factory->parse(meta_iter->second);
      }
    }
  }

  absl::node_hash_map<std::string, std::unique_ptr<const TypedMetadata::Object>> data_;
};

// MetadataPack is struct that contains both the proto and typed metadata.
template <class FactoryClass> struct MetadataPack {
  MetadataPack(const envoy::config::core::v3::Metadata& metadata)
      : proto_metadata_(metadata), typed_metadata_(proto_metadata_) {}
  MetadataPack() : proto_metadata_(), typed_metadata_(proto_metadata_) {}

  const envoy::config::core::v3::Metadata proto_metadata_;
  const TypedMetadataImpl<FactoryClass> typed_metadata_;
};

template <class FactoryClass> using MetadataPackPtr = std::unique_ptr<MetadataPack<FactoryClass>>;
template <class FactoryClass>
using MetadataPackSharedPtr = std::shared_ptr<MetadataPack<FactoryClass>>;

} // namespace Config
} // namespace Envoy
