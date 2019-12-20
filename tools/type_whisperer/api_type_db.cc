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

absl::optional<std::string> ApiTypeDb::getProtoPathForType(const std::string& type_name) {
  auto it = getApiTypeDb().types().find(type_name);
  if (it == getApiTypeDb().types().end()) {
    return absl::nullopt;
  }
  return it->second.proto_path();
}

} // namespace TypeWhisperer
} // namespace Tools
} // namespace Envoy
