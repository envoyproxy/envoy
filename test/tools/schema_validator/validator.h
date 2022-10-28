#pragma once

#include <string>

#include "envoy/api/api.h"

#include "source/common/stats/isolated_store_impl.h"

#include "test/test_common/utility.h"

namespace Envoy {

/**
 * Class for Schemas supported by validation tool.
 */
class Schema {
public:
  /**
   * List of supported schemas to validate.
   */
  enum Type { Bootstrap, DiscoveryResponse, Route };

  /**
   * Get a string representation of the schema type.
   * @param type to convert.
   * @return string representation of type.
   */
  static const std::string& toString(Type type);

private:
  static const std::string BOOTSTRAP;
  static const std::string DISCOVERY_RESPONSE;
  static const std::string ROUTE;
};

/**
 * Parses command line arguments for Schema Validator Tool.
 */
class Options {
public:
  Options(int argc, const char* const* argv);

  /**
   * @return the schema type.
   */
  Schema::Type schemaType() const { return schema_type_; }

  /**
   * @return the path to configuration file.
   */
  const std::string& configPath() const { return config_path_; }

  /**
   * @return whether to fail on deprecated fields.
   */
  bool failOnDeprecated() const { return fail_on_deprecated_; }

  /**
   * @return whether to fail on WiP fields.
   */
  bool failOnWip() const { return fail_on_wip_; }

private:
  Schema::Type schema_type_;
  std::string config_path_;
  bool fail_on_deprecated_;
  bool fail_on_wip_;
};

/**
 * Validates the schema of a configuration.
 */
class Validator {
public:
  Validator() : api_(Api::createApiForTest(stats_)) {}

  /**
   * Validates the configuration at config_path against schema_type.
   * An EnvoyException is thrown in several cases:
   *  - Cannot load the configuration from config_path(invalid path or malformed data).
   *  - A schema error from validating the configuration.
   *  - Use of deprecated/WiP fields if configured to fail in those cases.
   * @param options supplies the validation options.
   */
  void validate(const Options& options);

  /**
   * Run the validator from command line arguments.
   */
  static void run(int argc, const char* const* argv);

private:
  Stats::IsolatedStoreImpl stats_;
  Api::ApiPtr api_;
};

} // namespace Envoy
