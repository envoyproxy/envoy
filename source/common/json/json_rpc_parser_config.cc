#include "source/common/json/json_rpc_parser_config.h"

namespace Envoy {
namespace Json {

void JsonRpcParserConfig::addMethodConfig(absl::string_view method,
                                          std::vector<AttributeExtractionRule> fields) {
  method_fields_[std::string(method)] = std::move(fields);
}

const std::vector<AttributeExtractionRule>&
JsonRpcParserConfig::getFieldsForMethod(const std::string& method) const {
  static const std::vector<AttributeExtractionRule> empty;
  auto it = method_fields_.find(method);
  if (it != method_fields_.end()) {
    return it->second;
  }
  return empty;
}

} // namespace Json
} // namespace Envoy
