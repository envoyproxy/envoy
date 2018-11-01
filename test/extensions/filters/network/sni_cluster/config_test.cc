#include "envoy/config/filter/network/sni_cluster/v2/sni_cluster.pb.validate.h"

#include "extensions/filters/network/sni_cluster/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniCluster {

TEST(ConfigTest, ConfigTest) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  SniClusterNetworkFilterConfigFactory factory;
  envoy::config::filter::network::sni_cluster::v2::SniCluster config =
      *dynamic_cast<envoy::config::filter::network::sni_cluster::v2::SniCluster*>(
          factory.createEmptyConfigProto().get());
  auto* sni_substitution = config.mutable_sni_substitution();
  sni_substitution->set_regex("connection\\.sni");
  sni_substitution->set_replacement("replacement.sni");

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

} // namespace SniCluster
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
