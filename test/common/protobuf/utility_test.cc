#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "api/bootstrap.pb.h"
#include "gtest/gtest.h"

namespace Envoy {

TEST(UtilityTest, LoadBinaryProtoFromFile) {
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

TEST(UtilityTest, LoadTextProtoFromFile) {
  envoy::api::v2::Bootstrap bootstrap;
  bootstrap.mutable_cluster_manager()
      ->mutable_upstream_bind_config()
      ->mutable_source_address()
      ->set_address("1.1.1.1");

  ProtobufTypes::String bootstrap_text;
  ASSERT_TRUE(Protobuf::TextFormat::PrintToString(bootstrap, &bootstrap_text));
  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb_text", bootstrap_text);

  envoy::api::v2::Bootstrap proto_from_file;
  MessageUtil::loadFromFile(filename, proto_from_file);
  EXPECT_TRUE(TestUtility::protoEqual(bootstrap, proto_from_file));
}

TEST(UtilityTest, LoadTextProtoFromFile_Failure) {
  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb_text", "invalid {");

  envoy::api::v2::Bootstrap proto_from_file;
  EXPECT_THROW_WITH_MESSAGE(MessageUtil::loadFromFile(filename, proto_from_file), EnvoyException,
                            "Unable to parse file \"" + filename +
                                "\" as a text protobuf (type envoy.api.v2.Bootstrap)");
}

} // namespace Envoy
