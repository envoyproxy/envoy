#pragma once

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {

/**
 * Rule defining which attribute to extract based on its JSON path.
 */
struct AttributeExtractionRule {
  std::string path; // JSON path (e.g., "params.name")

  explicit AttributeExtractionRule(const std::string& p) : path(p) {}
};

/**
 * Configuration for the JSON-RPC parser, defining which fields should be
 * extracted per method or globally.
 */
class JsonRpcParserConfig {
public:
  virtual ~JsonRpcParserConfig() = default;

  /**
   * Returns the list of fields to extract for a specific JSON-RPC method.
   * @param method the method name.
   * @return vector of extraction rules.
   */
  const std::vector<AttributeExtractionRule> getFieldsForMethod(const std::string& method) const;

  /**
   * Adds a configuration for a specific method.
   * @param method the method name.
   * @param fields the rules for field extraction.
   */
  void addMethodConfig(absl::string_view method, std::vector<AttributeExtractionRule> fields);

  /**
   * @return global fields that should always be extracted regardless of the method.
   */
  const absl::flat_hash_set<std::string>& getAlwaysExtract() const { return always_extract_; }

protected:
  virtual void initializeDefaults() = 0;

  // Per-method field policies
  absl::flat_hash_map<std::string, std::vector<AttributeExtractionRule>> method_fields_;

  // Global fields to always extract
  absl::flat_hash_set<std::string> always_extract_;
};

} // namespace Json
} // namespace Envoy
