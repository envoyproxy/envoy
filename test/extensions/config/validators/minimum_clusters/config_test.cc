#include "envoy/extensions/config/validators/minimum_clusters/v3/minimum_clusters.pb.h"
#include "envoy/extensions/config/validators/minimum_clusters/v3/minimum_clusters.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/extensions/config/validators/minimum_clusters/config.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Config {
namespace Validators {
namespace {

TEST(MinimumClustersValidatorFactoryTest, CreateValidator) {
  auto factory = Registry::FactoryRegistry<Envoy::Config::ConfigValidatorFactory>::getFactory(
      "envoy.config.validators.minimum_clusters");
  EXPECT_NE(factory, nullptr);

  envoy::extensions::config::validators::minimum_clusters::v3::MinimumClustersValidator config;
  config.set_min_clusters_num(5);
  ProtobufWkt::Any typed_config;
  typed_config.PackFrom(config);
  auto validator =
      factory->createConfigValidator(typed_config, ProtobufMessage::getStrictValidationVisitor());
  EXPECT_NE(validator, nullptr);
}

TEST(MinimumClustersValidatorFactoryTest, CreateEmptyValidator) {
  auto factory = Registry::FactoryRegistry<Envoy::Config::ConfigValidatorFactory>::getFactory(
      "envoy.config.validators.minimum_clusters");
  EXPECT_NE(factory, nullptr);

  auto empty_proto = factory->createEmptyConfigProto();
  auto config = *dynamic_cast<
      envoy::extensions::config::validators::minimum_clusters::v3::MinimumClustersValidator*>(
      empty_proto.get());
  EXPECT_EQ(0, config.min_clusters_num());

  ProtobufWkt::Any typed_config;
  typed_config.PackFrom(config);
  auto validator =
      factory->createConfigValidator(typed_config, ProtobufMessage::getStrictValidationVisitor());
  EXPECT_NE(validator, nullptr);
}

} // namespace
} // namespace Validators
} // namespace Config
} // namespace Extensions
} // namespace Envoy
