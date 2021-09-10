#pragma once

#include "source/extensions/filters/network/common/factory_base.h"

#include "contrib/envoy/extensions/filters/network/kafka_mesh/v3alpha/kafka_mesh.pb.h"
#include "contrib/envoy/extensions/filters/network/kafka_mesh/v3alpha/kafka_mesh.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

using KafkaMeshProtoConfig = envoy::extensions::filters::network::kafka_mesh::v3alpha::KafkaMesh;

/**
 * Config registration for the Kafka mesh filter.
 */
class KafkaMeshConfigFactory : public Common::FactoryBase<KafkaMeshProtoConfig> {
public:
  KafkaMeshConfigFactory() : FactoryBase("envoy.filters.network.kafka_mesh", true) {}

private:
  Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const KafkaMeshProtoConfig& config,
                                    Server::Configuration::FactoryContext& context) override;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
