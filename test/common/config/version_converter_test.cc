#include "envoy/api/v2/cds.pb.h"
#include "envoy/service/cluster/v3alpha/cds.pb.h"

#include "common/config/api_version.h"
#include "common/config/version_converter.h"

#include "test/common/config/version_converter.pb.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

// Wire-style upgrading between versions.
TEST(VersionConverterTest, Upgrade) {
  API_NO_BOOST(envoy::api::v2::Cluster) source;
  source.set_drain_connections_on_host_removal(true);
  API_NO_BOOST(envoy::service::cluster::v3alpha::Cluster) dst;
  VersionConverter::upgrade(source, dst);
  EXPECT_TRUE(dst.ignore_health_on_host_removal());
}

// Downgrading to an earlier version (where it exists).
TEST(VersionConverterTest, DowngradeEarlier) {
  API_NO_BOOST(envoy::service::cluster::v3alpha::Cluster) source;
  source.set_ignore_health_on_host_removal(true);
  auto downgraded = VersionConverter::downgrade(source);
  const Protobuf::Descriptor* desc = downgraded->msg_->GetDescriptor();
  const Protobuf::Reflection* reflection = downgraded->msg_->GetReflection();
  EXPECT_EQ("envoy.api.v2.Cluster", desc->full_name());
  EXPECT_EQ(true, reflection->GetBool(*downgraded->msg_,
                                      desc->FindFieldByName("drain_connections_on_host_removal")));
}

// Downgrading is idempotent if no earlier version.
TEST(VersionConverterTest, DowngradeSame) {
  API_NO_BOOST(envoy::api::v2::Cluster) source;
  source.set_drain_connections_on_host_removal(true);
  auto downgraded = VersionConverter::downgrade(source);
  const Protobuf::Descriptor* desc = downgraded->msg_->GetDescriptor();
  const Protobuf::Reflection* reflection = downgraded->msg_->GetReflection();
  EXPECT_EQ("envoy.api.v2.Cluster", desc->full_name());
  EXPECT_EQ(true, reflection->GetBool(*downgraded->msg_,
                                      desc->FindFieldByName("drain_connections_on_host_removal")));
}

} // namespace
} // namespace Config
} // namespace Envoy
