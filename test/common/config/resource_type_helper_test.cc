#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.validate.h"

#include "source/common/config/resource_type_helper.h"
#include "source/common/protobuf/message_validator_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

TEST(ResourceTypeHelperTest, GetResourceName) {
  ProtobufMessage::NullValidationVisitorImpl validation_visitor;
  ResourceTypeHelper<envoy::config::cluster::v3::Cluster> helper(validation_visitor, "name");
  EXPECT_EQ("envoy.config.cluster.v3.Cluster", helper.getResourceName());
  EXPECT_NE(nullptr, helper.resourceDecoder());
}

} // namespace
} // namespace Config
} // namespace Envoy
