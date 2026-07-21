#include "source/common/config/metadata.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/type/metadata/v3/metadata.pb.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Config {

SINGLETON_MANAGER_REGISTRATION(const_metadata_shared_pool);

MetadataKey::MetadataKey(const envoy::type::metadata::v3::MetadataKey& metadata_key)
    : key_(metadata_key.key()) {
  for (const auto& seg : metadata_key.path()) {
    PathSegment path_seg;
    if (seg.has_key()) {
      path_seg.type_ = PathSegment::Type::Key;
      path_seg.key_ = seg.key();
    } else if (seg.has_index()) {
      path_seg.type_ = PathSegment::Type::Index;
      path_seg.index_ = seg.index();
    }
    path_.push_back(path_seg);
  }
}

const Protobuf::Value& Metadata::metadataValue(const envoy::config::core::v3::Metadata* metadata,
                                               const MetadataKey& metadata_key) {
  if (!metadata) {
    return Protobuf::Value::default_instance();
  }
  const auto filter_it = metadata->filter_metadata().find(metadata_key.key_);
  if (filter_it == metadata->filter_metadata().end()) {
    return Protobuf::Value::default_instance();
  }
  return structValue(filter_it->second, metadata_key.path_);
}

const Protobuf::Value& Metadata::metadataValue(const envoy::config::core::v3::Metadata* metadata,
                                               const std::string& filter,
                                               const std::vector<std::string>& path) {
  if (!metadata) {
    return Protobuf::Value::default_instance();
  }
  const auto filter_it = metadata->filter_metadata().find(filter);
  if (filter_it == metadata->filter_metadata().end()) {
    return Protobuf::Value::default_instance();
  }
  return structValue(filter_it->second, path);
}

const Protobuf::Value& Metadata::structValue(const Protobuf::Struct& struct_value,
                                             const std::vector<std::string>& path) {
  const Protobuf::Struct* data_struct = &struct_value;
  const Protobuf::Value* val = nullptr;
  // go through path to select sub entries
  for (const auto& p : path) {
    if (nullptr == data_struct) { // sub entry not found
      return Protobuf::Value::default_instance();
    }
    const auto entry_it = data_struct->fields().find(p);
    if (entry_it == data_struct->fields().end()) {
      return Protobuf::Value::default_instance();
    }
    val = &(entry_it->second);
    if (val->has_struct_value()) {
      data_struct = &(val->struct_value());
    } else {
      data_struct = nullptr;
    }
  }
  if (nullptr == val) {
    return Protobuf::Value::default_instance();
  }
  return *val;
}

const Protobuf::Value& Metadata::structValue(const Protobuf::Struct& struct_value,
                                             const std::vector<PathSegment>& path) {
  const Protobuf::Struct* data_struct = &struct_value;
  const Protobuf::Value* val = nullptr;

  for (const auto& segment : path) {
    if (segment.type_ == PathSegment::Type::Key) {
      // Handle struct field access
      if (nullptr == data_struct) {
        ENVOY_LOG_MISC(debug, "MetadataKey path segment expects Struct but found null");
        return Protobuf::Value::default_instance();
      }
      const auto entry_it = data_struct->fields().find(segment.key_);
      if (entry_it == data_struct->fields().end()) {
        ENVOY_LOG_MISC(debug, "MetadataKey key '{}' not found in Struct", segment.key_);
        return Protobuf::Value::default_instance();
      }
      val = &(entry_it->second);
      data_struct = val->has_struct_value() ? &(val->struct_value()) : nullptr;

    } else { // PathSegment::Type::Index
      // Handle list element access
      if (val == nullptr || val->kind_case() != Protobuf::Value::kListValue) {
        ENVOY_LOG_MISC(debug, "MetadataKey path segment expects ListValue but found {}",
                       static_cast<int>(val ? val->kind_case() : Protobuf::Value::KIND_NOT_SET));
        return Protobuf::Value::default_instance();
      }
      const auto& list = val->list_value();
      if (segment.index_ >= static_cast<uint32_t>(list.values_size())) {
        ENVOY_LOG_MISC(debug, "MetadataKey index {} out of bounds for list of size {}",
                       segment.index_, list.values_size());
        return Protobuf::Value::default_instance();
      }
      val = &(list.values(segment.index_));
      data_struct = val->has_struct_value() ? &(val->struct_value()) : nullptr;
    }
  }

  if (nullptr == val) {
    return Protobuf::Value::default_instance();
  }
  return *val;
}

const Protobuf::Value& Metadata::metadataValue(const envoy::config::core::v3::Metadata* metadata,
                                               const std::string& filter, const std::string& key) {
  const std::vector<std::string> path{key};
  return metadataValue(metadata, filter, path);
}

Protobuf::Value& Metadata::mutableMetadataValue(envoy::config::core::v3::Metadata& metadata,
                                                const std::string& filter, const std::string& key) {
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
  const Protobuf::Struct& data_struct = filter_it->second;
  const auto& fields = data_struct.fields();
  for (const auto& kv : label_set) {
    const auto entry_it = fields.find(kv.first);
    if (entry_it == fields.end()) {
      return false;
    }

    if (list_as_any && entry_it->second.kind_case() == Protobuf::Value::kListValue) {
      bool any_match = false;
      for (const auto& v : entry_it->second.list_value().values()) {
        if (ValueUtil::equal(v, kv.second)) {
          any_match = true;
          break;
        }
      }
      if (!any_match) {
        return false;
      }
    } else if (!ValueUtil::equal(entry_it->second, kv.second)) {
      return false;
    }
  }
  return true;
}

ConstMetadataSharedPoolSharedPtr
Metadata::getConstMetadataSharedPool(Singleton::Manager& manager, Event::Dispatcher& dispatcher) {
  return manager.getTyped<SharedPool::ObjectSharedPool<const envoy::config::core::v3::Metadata,
                                                       MessageUtil, MessageUtil>>(
      SINGLETON_MANAGER_REGISTERED_NAME(const_metadata_shared_pool), [&dispatcher] {
        return std::make_shared<SharedPool::ObjectSharedPool<
            const envoy::config::core::v3::Metadata, MessageUtil, MessageUtil>>(dispatcher);
      });
}

} // namespace Config
} // namespace Envoy
