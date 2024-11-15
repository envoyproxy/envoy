#include "tools/type_whisperer/api_type_db.h"

#include "source/common/protobuf/protobuf.h"

#include "tools/type_whisperer/api_type_db.pb.h"
#include "udpa/annotations/migrate.pb.h"

namespace Envoy {
namespace Tools {
namespace TypeWhisperer {

extern const char* AllProtosPbText;
extern const char* ApiTypeDbPbText;

namespace {

Protobuf::DescriptorPool* loadDescriptorPool() {
  Protobuf::FileDescriptorSet file_descriptor_set;
  if (Protobuf::TextFormat::ParseFromString(AllProtosPbText, &file_descriptor_set)) {
    auto* descriptor_pool = new Protobuf::DescriptorPool;
    // The file descriptor pb_text isn't topologically sorted, so need to relax
    // dependency checking here.
    descriptor_pool->AllowUnknownDependencies();
    for (const auto& file : file_descriptor_set.file()) {
      descriptor_pool->BuildFile(file);
    }
    return descriptor_pool;
  }
  return nullptr;
}

const Protobuf::DescriptorPool& getDescriptorPool() {
  static auto* descriptor_pool = loadDescriptorPool();
  return *descriptor_pool;
}

tools::type_whisperer::TypeDb* loadApiTypeDb() {
  auto* api_type_db = new tools::type_whisperer::TypeDb;
  if (Protobuf::TextFormat::ParseFromString(ApiTypeDbPbText, api_type_db)) {
    return api_type_db;
  }
  return nullptr;
}

const tools::type_whisperer::TypeDb& getApiTypeDb() {
  static auto* api_type_db = loadApiTypeDb();
  return *api_type_db;
}

} // namespace

absl::optional<TypeInformation>
ApiTypeDb::getExistingTypeInformation(const std::string& type_name) {
  auto it = getApiTypeDb().types().find(type_name);
  if (it == getApiTypeDb().types().end()) {
    return {};
  }
  return absl::make_optional<TypeInformation>(type_name, it->second.proto_path(), false);
}

absl::optional<TypeInformation> ApiTypeDb::getLatestTypeInformation(const std::string& type_name) {
  std::string latest_type_name;
  const tools::type_whisperer::TypeDbDescription* latest_type_desc{};
  std::string current_type_name = type_name;
  while (true) {
    auto it = getApiTypeDb().types().find(current_type_name);
    if (it == getApiTypeDb().types().end()) {
      break;
    }
    latest_type_name = it->first;
    latest_type_desc = &it->second;
    current_type_name = it->second.next_version_type_name();
  }
  if (latest_type_desc == nullptr) {
    return {};
  }
  const auto* enum_desc = getDescriptorPool().FindEnumTypeByName(type_name);
  if (enum_desc != nullptr) {
    auto result = absl::make_optional<TypeInformation>(latest_type_name,
                                                       latest_type_desc->proto_path(), true);
    for (int index = 0; index < enum_desc->value_count(); ++index) {
      const auto* value = enum_desc->value(index);
      if (value->options().HasExtension(udpa::annotations::enum_value_migrate)) {
        result->renames_[value->name()] =
            value->options().GetExtension(udpa::annotations::enum_value_migrate).rename();
      }
    }
    return result;
  }
  const auto* message_desc = getDescriptorPool().FindMessageTypeByName(type_name);
  if (message_desc != nullptr) {
    auto result = absl::make_optional<TypeInformation>(latest_type_name,
                                                       latest_type_desc->proto_path(), false);
    for (int index = 0; index < message_desc->field_count(); ++index) {
      const auto* field = message_desc->field(index);
      if (field->options().HasExtension(udpa::annotations::field_migrate)) {
        result->renames_[field->name()] =
            field->options().GetExtension(udpa::annotations::field_migrate).rename();
      }
    }
    return result;
  }
  return {};
}

} // namespace TypeWhisperer
} // namespace Tools
} // namespace Envoy
