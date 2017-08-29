#include "common/protobuf/utility.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "api/bootstrap.pb.h"
#include "gtest/gtest.h"

namespace Envoy {

TEST(UtilityTest, LoadProtoFromFile) {
  envoy::api::v2::Bootstrap bootstrap;
  bootstrap.mutable_cluster_manager()
      ->mutable_upstream_bind_config()
      ->mutable_source_address()
      ->set_address("1.1.1.1");

  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb", bootstrap.SerializeAsString());

  envoy::api::v2::Bootstrap proto_from_file;
  MessageUtil::loadFromFile(filename, proto_from_file);
  EXPECT_TRUE(TestUtility::protoEqual(bootstrap, proto_from_file));
}

} // namespace Envoy
