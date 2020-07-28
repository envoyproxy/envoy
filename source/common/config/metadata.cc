#include "common/config/metadata.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/type/metadata/v3/metadata.pb.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

SINGLETON_MANAGER_REGISTRATION(const_metadata_shared_pool);

MetadataKey::MetadataKey(const envoy::type::metadata::v3::MetadataKey& metadata_key)
    : key_(metadata_key.key()) {
  for (const auto& seg : metadata_key.path()) {
    path_.push_back(seg.key());
  }
}

const ProtobufWkt::Value& Metadata::metadataValue(const envoy::config::core::v3::Metadata* metadata,
                                                  const MetadataKey& metadata_key) {
  return metadataValue(metadata, metadata_key.key_, metadata_key.path_);
}

const ProtobufWkt::Value& Metadata::metadataValue(const envoy::config::core::v3::Metadata* metadata,
                                                  const std::string& filter,
                                                  const std::vector<std::string>& path) {
  if (!metadata) {
    return ProtobufWkt::Value::default_instance();
  }
  const auto filter_it = metadata->filter_metadata().find(filter);
  if (filter_it == metadata->filter_metadata().end()) {
    return ProtobufWkt::Value::default_instance();
  }
  const ProtobufWkt::Struct* data_struct = &(filter_it->second);
  const ProtobufWkt::Value* val = nullptr;
  // go through path to select sub entries
  for (const auto& p : path) {
    if (nullptr == data_struct) { // sub entry not found
      return ProtobufWkt::Value::default_instance();
    }
    const auto entry_it = data_struct->fields().find(p);
    if (entry_it == data_struct->fields().end()) {
      return ProtobufWkt::Value::default_instance();
    }
    val = &(entry_it->second);
    if (val->has_struct_value()) {
      data_struct = &(val->struct_value());
    } else {
      data_struct = nullptr;
    }
  }
  if (nullptr == val) {
    return ProtobufWkt::Value::default_instance();
  }
  return *val;
}

const ProtobufWkt::Value& Metadata::metadataValue(const envoy::config::core::v3::Metadata* metadata,
                                                  const std::string& filter,
                                                  const std::string& key) {
  const std::vector<std::string> path{key};
  return metadataValue(metadata, filter, path);
}

ProtobufWkt::Value& Metadata::mutableMetadataValue(envoy::config::core::v3::Metadata& metadata,
                                                   const std::string& filter,
                                                   const std::string& key) {
  return (*(*metadata.mutable_filter_metadata())[filter].mutable_fields())[key];
}

bool Metadata::metadataLabelMatch(const LabelSet& label_set,
                                  const envoy::config::core::v3::Metadata* host_metadata,
                                  const std::string& filter_key, bool list_as_any) {
  if (!host_metadata) {
    return label_set.empty();
  }
  const auto filter_it = host_metadata->filter_metadata().find(filter_key);
  if (filter_it == host_metadata->filter_metadata().end()) {
    return label_set.empty();
  }
  const ProtobufWkt::Struct& data_struct = filter_it->second;
  const auto& fields = data_struct.fields();
  for (const auto& [key, val] : label_set) {
    const auto entry_it = fields.find(key);
    if (entry_it == fields.end()) {
      return false;
    }

    if (list_as_any && entry_it->second.kind_case() == ProtobufWkt::Value::kListValue) {
      bool any_match = false;
      for (const auto& v : entry_it->second.list_value().values()) {
        if (ValueUtil::equal(v, val)) {
          any_match = true;
          break;
        }
      }
      if (!any_match) {
        return false;
      }
    } else if (!ValueUtil::equal(entry_it->second, val)) {
      return false;
    }
  }
  return true;
}

ConstMetadataSharedPoolSharedPtr
Metadata::getConstMetadataSharedPool(Singleton::Manager& manager, Event::Dispatcher& dispatcher) {
  return manager
      .getTyped<SharedPool::ObjectSharedPool<const envoy::config::core::v3::Metadata, MessageUtil>>(
          SINGLETON_MANAGER_REGISTERED_NAME(const_metadata_shared_pool), [&dispatcher] {
            return std::make_shared<
                SharedPool::ObjectSharedPool<const envoy::config::core::v3::Metadata, MessageUtil>>(
                dispatcher);
          });
}

} // namespace Config
} // namespace Envoy
