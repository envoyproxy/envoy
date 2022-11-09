#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Http {

/**
 * Interface class for early header mutation extensions.
 */
class EarlyHeaderMutation {
public:
  virtual ~EarlyHeaderMutation() = default;

  /**
   * Mutate the request headers at very early.
   *
   * @param headers request headers.
   * @return true if the mutation is could be continued for the flollowing extensions. Make no sense
   * if there is no following extensions.
   */
  virtual bool mutate(RequestHeaderMap& headers) const PURE;
};

using EarlyHeaderMutationPtr = std::unique_ptr<EarlyHeaderMutation>;

/*
 * A factory for creating early header mutation extensions.
 */
class EarlyHeaderMutationFactory : public Envoy::Config::TypedFactory {
public:
  ~EarlyHeaderMutationFactory() override = default;

  /**
   * Creates a particular extension implementation.
   *
   * @param config supplies the configuration for the early mutation extension.
   * @return EarlyHeaderMutationPtr the extension instance.
   */
  virtual EarlyHeaderMutationPtr
  createExtension(const Protobuf::Message& config,
                  Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "envoy.http.early_header_mutation"; }
};

using EarlyHeaderMutationFactoryPtr = std::unique_ptr<EarlyHeaderMutationFactory>;

} // namespace Http
} // namespace Envoy
