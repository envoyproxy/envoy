#include "tools/type_whisperer/api_type_db.h"

#include "common/protobuf/protobuf.h"

#include "tools/type_whisperer/api_type_db.pb.h"

namespace Envoy {
namespace Tools {
namespace TypeWhisperer {

extern const char* ApiTypeDbPbText;

namespace {

tools::type_whisperer::TypeDb* loadApiTypeDb() {
  tools::type_whisperer::TypeDb* api_type_db = new tools::type_whisperer::TypeDb;
  if (Protobuf::TextFormat::ParseFromString(ApiTypeDbPbText, api_type_db)) {
    return api_type_db;
  }
  return nullptr;
}

const tools::type_whisperer::TypeDb& getApiTypeDb() {
  static tools::type_whisperer::TypeDb* api_type_db = loadApiTypeDb();
  return *api_type_db;
}

} // namespace

absl::optional<TypeInformation> ApiTypeDb::getLatestTypeInformation(const std::string& type_name) {
  absl::string_view latest_type_name;
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
  auto result = absl::make_optional<TypeInformation>(latest_type_name,
                                                     latest_type_desc->type_details().proto_path(),
                                                     latest_type_desc->type_details().enum_type());
  for (const auto it : latest_type_desc->type_details().fields()) {
    result->field_renames_[it.first] = it.second.rename();
  }
  return result;
}

} // namespace TypeWhisperer
} // namespace Tools
} // namespace Envoy
