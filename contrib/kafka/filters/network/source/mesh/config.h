#pragma once

#include "envoy/extensions/filters/network/kafka_mesh/v3alpha/kafka_mesh.pb.h"
#include "envoy/extensions/filters/network/kafka_mesh/v3alpha/kafka_mesh.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

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
  KafkaMeshConfigFactory() : FactoryBase(NetworkFilterNames::get().KafkaMesh, true) {}

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
