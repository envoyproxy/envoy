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
  absl::optional<TypeInformation> result;
  std::string current_type_name = type_name;
  while (true) {
    auto it = getApiTypeDb().types().find(current_type_name);
    if (it == getApiTypeDb().types().end()) {
      return result;
    }
    result.emplace(current_type_name, it->second.proto_path());
    current_type_name = it->second.next_version_type_name();
  }
}

} // namespace TypeWhisperer
} // namespace Tools
} // namespace Envoy
