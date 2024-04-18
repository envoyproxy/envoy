#pragma once

#include <string>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/config/subscription.h"
#include "envoy/server/instance.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

/**
 * The configuration validator interface. One can implement such validator
 * to add custom constraints to the fetched config, and reject a config
 * which violates these constraints.
 * The validators will be extensions that can be dynamically configured.
 *
 * A validator example: a validator that prevents removing all Clusters
 * from an Envoy (which may be caused by a bug in the config plane, and not
 * intentional change).
 */
class ConfigValidator {
public:
  virtual ~ConfigValidator() = default;

  /**
   * Validates a given set of resources matching a State-of-the-World update.
   * @param server A server instance to fetch the state before applying the config.
   * @param resources List of decoded resources that reflect the new state.
   * @throw EnvoyException if the config should be rejected.
   */
  virtual void validate(const Server::Instance& server,
                        const std::vector<DecodedResourcePtr>& resources) PURE;

  /**
   * Validates a given set of resources matching an Incremental update.
   * @param server A server instance to fetch the state before applying the config.
   * @param added_resources A list of decoded resources to add to the current state.
   * @param removed_resources A list of resources to remove from the current state.
   * @throw EnvoyException if the config should be rejected.
   */
  virtual void validate(const Server::Instance& server,
                        const std::vector<DecodedResourcePtr>& added_resources,
                        const Protobuf::RepeatedPtrField<std::string>& removed_resources) PURE;
};

using ConfigValidatorPtr = std::unique_ptr<ConfigValidator>;

/**
 * A factory abstract class for creating instances of ConfigValidators.
 */
class ConfigValidatorFactory : public Config::TypedFactory {
public:
  ~ConfigValidatorFactory() override = default;

  /**
   * Creates a ConfigValidator using the given config.
   */
  virtual ConfigValidatorPtr
  createConfigValidator(const ProtobufWkt::Any& config,
                        ProtobufMessage::ValidationVisitor& validation_visitor) PURE;

  std::string category() const override { return "envoy.config.validators"; }

  /**
   * Returns the xDS service type url that the config validator expects to receive.
   */
  virtual std::string typeUrl() const PURE;
};

} // namespace Config
} // namespace Envoy
