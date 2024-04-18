#include "contrib/common/sqlutils/source/sqlutils.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace SQLUtils {

bool SQLUtils::setMetadata(const std::string& query, const DecoderAttributes& attr,
                           ProtobufWkt::Struct& metadata) {
  hsql::SQLParserResult result;

  hsql::SQLParser::parse(query, &result);

  if (!result.isValid()) {
    return false;
  }

  std::string database;
  // Check if the attributes map contains database name.
  const auto it = attr.find("database");
  if (it != attr.end()) {
    database = absl::StrCat(".", it->second);
  }

  auto& fields = *metadata.mutable_fields();

  for (auto i = 0u; i < result.size(); ++i) {
    if (result.getStatement(i)->type() == hsql::StatementType::kStmtShow) {
      continue;
    }
    hsql::TableAccessMap table_access_map;
    // Get names of accessed tables.
    result.getStatement(i)->tablesAccessed(table_access_map);
    for (auto& it : table_access_map) {
      auto& operations = *fields[it.first + database].mutable_list_value();
      // For each table get names of operations performed on that table.
      for (const auto& ot : it.second) {
        operations.add_values()->set_string_value(ot);
      }
    }
  }

  return true;
}

} // namespace SQLUtils
} // namespace Common
} // namespace Extensions
} // namespace Envoy
