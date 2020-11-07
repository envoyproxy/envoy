#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "common/config/api_version.h"

#include "test/config_test/config_test.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::HasSubstr;
using testing::StartsWith;

namespace Envoy {

// A deprecated field can be used in previous version text proto and upgraded.
TEST(DeprecatedConfigsTest, DEPRECATED_FEATURE_TEST(LoadV2BootstrapTextProtoDeprecatedField)) {
  API_NO_BOOST(envoy::config::bootstrap::v2::Bootstrap)
  bootstrap = TestUtility::parseYaml<envoy::config::bootstrap::v2::Bootstrap>(R"EOF(
    node:
      build_version: foo
    )EOF");

  std::string bootstrap_text;
  ASSERT_TRUE(Protobuf::TextFormat::PrintToString(bootstrap, &bootstrap_text));
  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb_text", bootstrap_text);

  // Loading as previous version should work (after upgrade)
  API_NO_BOOST(envoy::config::bootstrap::v3::Bootstrap) proto_v2_from_file;
  EXPECT_LOG_CONTAINS("warning", "Using deprecated option 'envoy.api.v2.core.Node.build_version'",
                      ConfigTest::loadVersionedBootstrapFile(filename, proto_v2_from_file, 2));
  EXPECT_EQ("foo", proto_v2_from_file.node().hidden_envoy_deprecated_build_version());

  // Loading as current version should fail
  API_NO_BOOST(envoy::config::bootstrap::v3::Bootstrap) proto_v3_from_file;
  EXPECT_THAT_THROWS_MESSAGE(
      ConfigTest::loadVersionedBootstrapFile(filename, proto_v3_from_file, 3), EnvoyException,
      AllOf(StartsWith("Unable to parse file"),
            HasSubstr("as a text protobuf (type envoy.config.bootstrap.v3.Bootstrap)")));

  API_NO_BOOST(envoy::config::bootstrap::v3::Bootstrap)
  bootstrap_v3 = TestUtility::parseYaml<envoy::config::bootstrap::v3::Bootstrap>(R"EOF(
    node:
      hidden_envoy_deprecated_build_version: foo
    )EOF");

  std::string bootstrap_text_v3;
  ASSERT_TRUE(Protobuf::TextFormat::PrintToString(bootstrap_v3, &bootstrap_text_v3));
  const std::string filename_v3 =
      TestEnvironment::writeStringToFileForTest("proto_v3.pb_text", bootstrap_text_v3);

  // Loading v3 with hidden-deprecated field as current version should fail
  EXPECT_THAT_THROWS_MESSAGE(
      ConfigTest::loadVersionedBootstrapFile(filename_v3, proto_v3_from_file, 3), EnvoyException,
      HasSubstr("Illegal use of hidden_envoy_deprecated_ V2 field "
                "'envoy.config.core.v3.Node.hidden_envoy_deprecated_build_version'"));

  // Loading v3 with hidden-deprecated field with boosting should fail as it
  // doesn't appear in v2 and only in v3 but marked as hidden_envoy_deprecated
  EXPECT_THAT_THROWS_MESSAGE(
      ConfigTest::loadVersionedBootstrapFile(filename_v3, proto_v3_from_file), EnvoyException,
      HasSubstr("Illegal use of hidden_envoy_deprecated_ V2 field "
                "'envoy.config.core.v3.Node.hidden_envoy_deprecated_build_version'"));
}

// A deprecated field can be used in previous version binary proto and upgraded.
TEST(DeprecatedConfigsTest, DEPRECATED_FEATURE_TEST(LoadV2BootstrapBinaryProtoDeprecatedField)) {
  API_NO_BOOST(envoy::config::bootstrap::v2::Bootstrap)
  bootstrap = TestUtility::parseYaml<envoy::config::bootstrap::v2::Bootstrap>(R"EOF(
    node:
      build_version: foo
    )EOF");

  std::string bootstrap_binary_str;
  bootstrap_binary_str.reserve(bootstrap.ByteSizeLong());
  bootstrap.SerializeToString(&bootstrap_binary_str);
  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb", bootstrap_binary_str);

  // Loading as previous version should work (after upgrade)
  API_NO_BOOST(envoy::config::bootstrap::v3::Bootstrap) proto_v2_from_file;
  EXPECT_LOG_CONTAINS("warning", "Using deprecated option 'envoy.api.v2.core.Node.build_version'",
                      ConfigTest::loadVersionedBootstrapFile(filename, proto_v2_from_file, 2));
  EXPECT_EQ("foo", proto_v2_from_file.node().hidden_envoy_deprecated_build_version());

  // Loading as current version should fail
  API_NO_BOOST(envoy::config::bootstrap::v3::Bootstrap) proto_v3_from_file;
  EXPECT_THAT_THROWS_MESSAGE(
      ConfigTest::loadVersionedBootstrapFile(filename, proto_v3_from_file, 3), EnvoyException,
      HasSubstr("Illegal use of hidden_envoy_deprecated_ V2 field "
                "'envoy.config.core.v3.Node.hidden_envoy_deprecated_build_version'"));

  API_NO_BOOST(envoy::config::bootstrap::v3::Bootstrap)
  bootstrap_v3 = TestUtility::parseYaml<envoy::config::bootstrap::v3::Bootstrap>(R"EOF(
    node:
      hidden_envoy_deprecated_build_version: foo
    )EOF");

  std::string bootstrap_binary_str_v3;
  bootstrap_binary_str_v3.reserve(bootstrap.ByteSizeLong());
  bootstrap.SerializeToString(&bootstrap_binary_str_v3);
  const std::string filename_v3 =
      TestEnvironment::writeStringToFileForTest("proto_v3.pb", bootstrap_binary_str_v3);

  // Loading v3 with hidden-deprecated field as current version should fail
  EXPECT_THAT_THROWS_MESSAGE(
      ConfigTest::loadVersionedBootstrapFile(filename_v3, proto_v3_from_file, 3), EnvoyException,
      HasSubstr("Illegal use of hidden_envoy_deprecated_ V2 field "
                "'envoy.config.core.v3.Node.hidden_envoy_deprecated_build_version'"));

  // Loading binary proto v3 with hidden-deprecated field with boosting will
  // succeed as it cannot differentiate between v2 with the deprecated field and
  // v3 with hidden_envoy_deprecated field
  ConfigTest::loadVersionedBootstrapFile(filename_v3, proto_v3_from_file);
  EXPECT_EQ("foo", proto_v3_from_file.node().hidden_envoy_deprecated_build_version());
}

// A deprecated field can be used in previous version yaml and upgraded.
TEST(DeprecatedConfigsTest, DEPRECATED_FEATURE_TEST(LoadV2BootstrapYamlDeprecatedField)) {
  API_NO_BOOST(envoy::config::bootstrap::v2::Bootstrap)
  bootstrap = TestUtility::parseYaml<envoy::config::bootstrap::v2::Bootstrap>(R"EOF(
    node:
      build_version: foo
    )EOF");

  EXPECT_EQ("node:\n  build_version: foo",
            MessageUtil::getYamlStringFromMessage(bootstrap, true, false));
  const std::string filename = TestEnvironment::writeStringToFileForTest(
      "proto.yaml", MessageUtil::getYamlStringFromMessage(bootstrap, false, false));

  // Loading as previous version should work (after upgrade)
  API_NO_BOOST(envoy::config::bootstrap::v3::Bootstrap) proto_v2_from_file;
  EXPECT_LOG_CONTAINS("warning", "Using deprecated option 'envoy.api.v2.core.Node.build_version'",
                      ConfigTest::loadVersionedBootstrapFile(filename, proto_v2_from_file, 2));
  EXPECT_EQ("foo", proto_v2_from_file.node().hidden_envoy_deprecated_build_version());

  // Loading as current version should fail
  API_NO_BOOST(envoy::config::bootstrap::v3::Bootstrap) proto_v3_from_file;
  EXPECT_THAT_THROWS_MESSAGE(
      ConfigTest::loadVersionedBootstrapFile(filename, proto_v3_from_file, 3), EnvoyException,
      AllOf(HasSubstr("type envoy.config.bootstrap.v3.Bootstrap"),
            HasSubstr("build_version: Cannot find field")));

  API_NO_BOOST(envoy::config::bootstrap::v3::Bootstrap)
  bootstrap_v3 = TestUtility::parseYaml<envoy::config::bootstrap::v3::Bootstrap>(R"EOF(
    node:
      hidden_envoy_deprecated_build_version: foo
    )EOF");

  EXPECT_EQ("node:\n  hidden_envoy_deprecated_build_version: foo",
            MessageUtil::getYamlStringFromMessage(bootstrap_v3, true, false));
  const std::string filename_v3 = TestEnvironment::writeStringToFileForTest(
      "proto_v3.yaml", MessageUtil::getYamlStringFromMessage(bootstrap_v3, false, false));

  // Loading v3 with hidden-deprecated field as current version should fail
  EXPECT_THAT_THROWS_MESSAGE(
      ConfigTest::loadVersionedBootstrapFile(filename_v3, proto_v3_from_file, 3), EnvoyException,
      HasSubstr("Illegal use of hidden_envoy_deprecated_ V2 field "
                "'envoy.config.core.v3.Node.hidden_envoy_deprecated_build_version'"));

  // Loading v3 with hidden-deprecated field with boosting should fail as the name
  // doesn't appear in v2 and only in v3 but marked as hidden_envoy_deprecated
  EXPECT_THAT_THROWS_MESSAGE(
      ConfigTest::loadVersionedBootstrapFile(filename_v3, proto_v3_from_file), EnvoyException,
      HasSubstr("Illegal use of hidden_envoy_deprecated_ V2 field "
                "'envoy.config.core.v3.Node.hidden_envoy_deprecated_build_version'"));
}

// A deprecated field can be used in previous version json and upgraded.
TEST(DeprecatedConfigsTest, DEPRECATED_FEATURE_TEST(LoadV2BootstrapJsonDeprecatedField)) {
  API_NO_BOOST(envoy::config::bootstrap::v2::Bootstrap)
  bootstrap = TestUtility::parseYaml<envoy::config::bootstrap::v2::Bootstrap>(R"EOF(
    node:
      build_version: foo
    )EOF");

  EXPECT_EQ("{\"node\":{\"build_version\":\"foo\"}}",
            MessageUtil::getJsonStringFromMessage(bootstrap, false, false));
  const std::string filename = TestEnvironment::writeStringToFileForTest(
      "proto.json", MessageUtil::getJsonStringFromMessage(bootstrap, false, false));

  // Loading as previous version should work (after upgrade)
  API_NO_BOOST(envoy::config::bootstrap::v3::Bootstrap) proto_v2_from_file;
  EXPECT_LOG_CONTAINS("warning", "Using deprecated option 'envoy.api.v2.core.Node.build_version'",
                      ConfigTest::loadVersionedBootstrapFile(filename, proto_v2_from_file, 2));
  EXPECT_EQ("foo", proto_v2_from_file.node().hidden_envoy_deprecated_build_version());

  // Loading as current version should fail
  API_NO_BOOST(envoy::config::bootstrap::v3::Bootstrap) proto_v3_from_file;
  EXPECT_THROW_WITH_MESSAGE(
      ConfigTest::loadVersionedBootstrapFile(filename, proto_v3_from_file, 3), EnvoyException,
      "Protobuf message (type envoy.config.bootstrap.v3.Bootstrap reason INVALID_ARGUMENT:(node) "
      "build_version: Cannot find field.) has unknown fields");

  API_NO_BOOST(envoy::config::bootstrap::v3::Bootstrap)
  bootstrap_v3 = TestUtility::parseYaml<envoy::config::bootstrap::v3::Bootstrap>(R"EOF(
    node:
      hidden_envoy_deprecated_build_version: foo
    )EOF");

  EXPECT_EQ("{\"node\":{\"hidden_envoy_deprecated_build_version\":\"foo\"}}",
            MessageUtil::getJsonStringFromMessage(bootstrap_v3, false, false));
  const std::string filename_v3 = TestEnvironment::writeStringToFileForTest(
      "proto_v3.json", MessageUtil::getYamlStringFromMessage(bootstrap_v3, false, false));

  // Loading v3 with hidden-deprecated field as current version should fail
  EXPECT_THAT_THROWS_MESSAGE(
      ConfigTest::loadVersionedBootstrapFile(filename_v3, proto_v3_from_file, 3), EnvoyException,
      AllOf(StartsWith("Unable to parse JSON as proto"),
            HasSubstr("hidden_envoy_deprecated_build_version: foo")));

  // Loading v3 with hidden-deprecated field with boosting should fail as the name
  // doesn't appear in v2 and only in v3 but marked as hidden_envoy_deprecated
  EXPECT_THAT_THROWS_MESSAGE(
      ConfigTest::loadVersionedBootstrapFile(filename_v3, proto_v3_from_file), EnvoyException,
      AllOf(StartsWith("Unable to parse JSON as proto"),
            HasSubstr("hidden_envoy_deprecated_build_version: foo")));
}

// Test the config_proto option when loading from bootstrap
TEST(DeprecatedConfigsTest, DEPRECATED_FEATURE_TEST(LoadV2BootstrapConfigProtoDeprecatedField)) {
  API_NO_BOOST(envoy::config::bootstrap::v3::Bootstrap)
  in_bootstrap_v3 = TestUtility::parseYaml<envoy::config::bootstrap::v3::Bootstrap>(R"EOF(
    node:
      hidden_envoy_deprecated_build_version: foo
    )EOF");

  // Loading v3 with hidden-deprecated field as current version should fail
  API_NO_BOOST(envoy::config::bootstrap::v3::Bootstrap) proto_v3_from_file;
  EXPECT_THAT_THROWS_MESSAGE(
      ConfigTest::loadBootstrapConfigProto(in_bootstrap_v3, proto_v3_from_file), EnvoyException,
      HasSubstr("Illegal use of hidden_envoy_deprecated_ V2 field "
                "'envoy.config.core.v3.Node.hidden_envoy_deprecated_build_version'"));
}

} // namespace Envoy
