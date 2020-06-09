#pragma once

#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/server/instance.h"
#include "server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Circonus {

/**
 * Stats sink configuration for {@code Circonus}.  Unlike other
 * sinks this factory registers an admin handler suitable for
 * polling
 */
class CirconusSinkFactory final : Logger::Loggable<Logger::Id::config>,
                                  public Server::Configuration::StatsSinkFactory {
public:
  virtual ~CirconusSinkFactory() override = default;

  virtual Stats::SinkPtr createStatsSink(const Protobuf::Message& config,
                                         Server::Instance& server) override;
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  virtual std::string name() const override;
};

} // namespace Circonus
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
