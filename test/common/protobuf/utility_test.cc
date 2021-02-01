#include "envoy/api/v2/cluster.pb.h"
#include "envoy/api/v2/core/base.pb.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.validate.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.validate.h"
#include "envoy/config/cluster/v3/filter.pb.h"
#include "envoy/config/cluster/v3/filter.pb.validate.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "common/common/base64.h"
#include "common/config/api_version.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/runtime/runtime_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/proto/deprecated.pb.h"
#include "test/proto/sensitive.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/container/node_hash_set.h"
#include "gtest/gtest.h"
#include "udpa/type/v1/typed_struct.pb.h"

using namespace std::chrono_literals;

namespace Envoy {

using testing::AllOf;
using testing::HasSubstr;
using testing::Property;

class RuntimeStatsHelper : public TestScopedRuntime {
public:
  RuntimeStatsHelper(bool allow_deprecated_v2_api = false)
      : runtime_deprecated_feature_use_(store_.counter("runtime.deprecated_feature_use")),
        deprecated_feature_seen_since_process_start_(
            store_.gauge("runtime.deprecated_feature_seen_since_process_start",
                         Stats::Gauge::ImportMode::NeverImport)) {
    if (allow_deprecated_v2_api) {
      Runtime::LoaderSingleton::getExisting()->mergeValues({
          {"envoy.reloadable_features.enable_deprecated_v2_api", "true"},
          {"envoy.features.enable_all_deprecated_features", "true"},
      });
    }
  }

  Stats::Counter& runtime_deprecated_feature_use_;
  Stats::Gauge& deprecated_feature_seen_since_process_start_;
};

class ProtobufUtilityTest : public testing::Test, protected RuntimeStatsHelper {};
// TODO(htuch): During/before the v2 removal, cleanup the various examples that explicitly refer to
// v2 API protos and replace with upgrade examples not tie to the concrete API.
class ProtobufV2ApiUtilityTest : public testing::Test, protected RuntimeStatsHelper {
public:
  ProtobufV2ApiUtilityTest() : RuntimeStatsHelper(true) {}
};

TEST_F(ProtobufUtilityTest, ConvertPercentNaNDouble) {
  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
  common_config_.mutable_healthy_panic_threshold()->set_value(
      std::numeric_limits<double>::quiet_NaN());
  EXPECT_THROW(PROTOBUF_PERCENT_TO_DOUBLE_OR_DEFAULT(common_config_, healthy_panic_threshold, 0.5),
               EnvoyException);
}

TEST_F(ProtobufUtilityTest, ConvertPercentNaN) {
  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
  common_config_.mutable_healthy_panic_threshold()->set_value(
      std::numeric_limits<double>::quiet_NaN());
  EXPECT_THROW(PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(common_config_,
                                                              healthy_panic_threshold, 100, 50),
               EnvoyException);
}

namespace ProtobufPercentHelper {

TEST_F(ProtobufUtilityTest, EvaluateFractionalPercent) {
  { // 0/100 (default)
    envoy::type::v3::FractionalPercent percent;
    EXPECT_FALSE(evaluateFractionalPercent(percent, 0));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 50));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 100));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 1000));
  }
  { // 5/100
    envoy::type::v3::FractionalPercent percent;
    percent.set_numerator(5);
    EXPECT_TRUE(evaluateFractionalPercent(percent, 0));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 4));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 5));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 50));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 100));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 104));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 105));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 204));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 1000));
  }
  { // 75/100
    envoy::type::v3::FractionalPercent percent;
    percent.set_numerator(75);
    EXPECT_TRUE(evaluateFractionalPercent(percent, 0));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 4));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 5));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 74));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 80));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 100));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 104));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 105));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 200));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 274));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 280));
  }
  { // 5/10000
    envoy::type::v3::FractionalPercent percent;
    percent.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);
    percent.set_numerator(5);
    EXPECT_TRUE(evaluateFractionalPercent(percent, 0));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 4));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 5));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 50));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 100));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 9000));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 10000));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 10004));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 10005));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 20004));
  }
  { // 5/MILLION
    envoy::type::v3::FractionalPercent percent;
    percent.set_denominator(envoy::type::v3::FractionalPercent::MILLION);
    percent.set_numerator(5);
    EXPECT_TRUE(evaluateFractionalPercent(percent, 0));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 4));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 5));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 50));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 100));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 9000));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 10000));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 10004));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 10005));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 900005));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 900000));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 1000000));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 1000004));
    EXPECT_FALSE(evaluateFractionalPercent(percent, 1000005));
    EXPECT_TRUE(evaluateFractionalPercent(percent, 2000004));
  }
}

} // namespace ProtobufPercentHelper

TEST_F(ProtobufUtilityTest, MessageUtilHash) {
  ProtobufWkt::Struct s;
  (*s.mutable_fields())["ab"].set_string_value("fgh");
  (*s.mutable_fields())["cde"].set_string_value("ij");

  ProtobufWkt::Any a1;
  a1.PackFrom(s);
  // The two base64 encoded Struct to test map is identical to the struct above, this tests whether
  // a map is deterministically serialized and hashed.
  ProtobufWkt::Any a2 = a1;
  a2.set_value(Base64::decode("CgsKA2NkZRIEGgJpagoLCgJhYhIFGgNmZ2g="));
  ProtobufWkt::Any a3 = a1;
  a3.set_value(Base64::decode("CgsKAmFiEgUaA2ZnaAoLCgNjZGUSBBoCaWo="));

  EXPECT_EQ(MessageUtil::hash(a1), MessageUtil::hash(a2));
  EXPECT_EQ(MessageUtil::hash(a2), MessageUtil::hash(a3));
  EXPECT_NE(0, MessageUtil::hash(a1));
  EXPECT_NE(MessageUtil::hash(s), MessageUtil::hash(a1));
}

TEST_F(ProtobufUtilityTest, MessageUtilHashAndEqualToIgnoreOriginalTypeField) {
  ProtobufWkt::Struct s;
  (*s.mutable_fields())["ab"].set_string_value("fgh");
  EXPECT_EQ(1, s.fields_size());
  envoy::api::v2::core::Metadata mv2;
  mv2.mutable_filter_metadata()->insert({"xyz", s});
  EXPECT_EQ(1, mv2.filter_metadata_size());

  // Add the OriginalTypeFieldNumber as unknown field.
  envoy::config::core::v3::Metadata mv3;
  Config::VersionConverter::upgrade(mv2, mv3);

  // Add another unknown field.
  {
    const Protobuf::Reflection* reflection = mv3.GetReflection();
    auto* unknown_field_set = reflection->MutableUnknownFields(&mv3);
    auto set_size = unknown_field_set->field_count();
    // 183412668 is the magic number OriginalTypeFieldNumber. The successor number should not be
    // occupied.
    unknown_field_set->AddFixed32(183412668 + 1, 1);
    EXPECT_EQ(set_size + 1, unknown_field_set->field_count()) << "Fail to add an unknown field";
  }

  envoy::config::core::v3::Metadata mv3dup = mv3;
  ASSERT_EQ(MessageUtil::hash(mv3), MessageUtil::hash(mv3dup));
  ASSERT(MessageUtil()(mv3, mv3dup));
}

TEST_F(ProtobufUtilityTest, RepeatedPtrUtilDebugString) {
  Protobuf::RepeatedPtrField<ProtobufWkt::UInt32Value> repeated;
  EXPECT_EQ("[]", RepeatedPtrUtil::debugString(repeated));
  repeated.Add()->set_value(10);
  EXPECT_EQ("[value: 10\n]", RepeatedPtrUtil::debugString(repeated));
  repeated.Add()->set_value(20);
  EXPECT_EQ("[value: 10\n, value: 20\n]", RepeatedPtrUtil::debugString(repeated));
}

// Validated exception thrown when downcastAndValidate observes a PGV failures.
TEST_F(ProtobufUtilityTest, DowncastAndValidateFailedValidation) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  bootstrap.mutable_static_resources()->add_clusters();
  EXPECT_THROW(TestUtility::validate(bootstrap), ProtoValidationException);
  EXPECT_THROW(
      TestUtility::downcastAndValidate<const envoy::config::bootstrap::v3::Bootstrap&>(bootstrap),
      ProtoValidationException);
}

// Validated exception thrown when downcastAndValidate observes a unknown field.
TEST_F(ProtobufUtilityTest, DowncastAndValidateUnknownFields) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  bootstrap.GetReflection()->MutableUnknownFields(&bootstrap)->AddVarint(1, 0);
  EXPECT_THROW_WITH_MESSAGE(TestUtility::validate(bootstrap), EnvoyException,
                            "Protobuf message (type envoy.config.bootstrap.v3.Bootstrap with "
                            "unknown field set {1}) has unknown fields");
  EXPECT_THROW_WITH_MESSAGE(TestUtility::validate(bootstrap), EnvoyException,
                            "Protobuf message (type envoy.config.bootstrap.v3.Bootstrap with "
                            "unknown field set {1}) has unknown fields");
}

// Validated exception thrown when downcastAndValidate observes a nested unknown field.
TEST_F(ProtobufUtilityTest, DowncastAndValidateUnknownFieldsNested) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
  cluster->GetReflection()->MutableUnknownFields(cluster)->AddVarint(1, 0);
  EXPECT_THROW_WITH_MESSAGE(TestUtility::validate(*cluster), EnvoyException,
                            "Protobuf message (type envoy.config.cluster.v3.Cluster with "
                            "unknown field set {1}) has unknown fields");
  EXPECT_THROW_WITH_MESSAGE(TestUtility::validate(bootstrap), EnvoyException,
                            "Protobuf message (type envoy.config.cluster.v3.Cluster with "
                            "unknown field set {1}) has unknown fields");
}

TEST_F(ProtobufUtilityTest, JsonConvertAnyUnknownMessageType) {
  ProtobufWkt::Any source_any;
  source_any.set_type_url("type.googleapis.com/bad.type.url");
  source_any.set_value("asdf");
  EXPECT_THAT(MessageUtil::getJsonStringFromMessage(source_any, true).status(),
              AllOf(Property(&ProtobufUtil::Status::ok, false),
                    Property(&ProtobufUtil::Status::ToString, testing::HasSubstr("bad.type.url"))));
}

TEST_F(ProtobufUtilityTest, JsonConvertKnownGoodMessage) {
  ProtobufWkt::Any source_any;
  source_any.PackFrom(envoy::config::bootstrap::v3::Bootstrap::default_instance());
  EXPECT_THAT(MessageUtil::getJsonStringFromMessageOrDie(source_any, true),
              testing::HasSubstr("@type"));
}

TEST_F(ProtobufUtilityTest, JsonConvertOrErrorAnyWithUnknownMessageType) {
  ProtobufWkt::Any source_any;
  source_any.set_type_url("type.googleapis.com/bad.type.url");
  source_any.set_value("asdf");
  EXPECT_THAT(MessageUtil::getJsonStringFromMessageOrError(source_any), HasSubstr("unknown type"));
}

TEST_F(ProtobufUtilityTest, JsonConvertOrDieAnyWithUnknownMessageType) {
  ProtobufWkt::Any source_any;
  source_any.set_type_url("type.googleapis.com/bad.type.url");
  source_any.set_value("asdf");
  EXPECT_DEATH(MessageUtil::getJsonStringFromMessageOrDie(source_any), "bad.type.url");
}

TEST_F(ProtobufUtilityTest, LoadBinaryProtoFromFile) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  bootstrap.mutable_cluster_manager()
      ->mutable_upstream_bind_config()
      ->mutable_source_address()
      ->set_address("1.1.1.1");

  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb", bootstrap.SerializeAsString());

  envoy::config::bootstrap::v3::Bootstrap proto_from_file;
  TestUtility::loadFromFile(filename, proto_from_file, *api_);
  EXPECT_EQ(0, runtime_deprecated_feature_use_.value());
  EXPECT_TRUE(TestUtility::protoEqual(bootstrap, proto_from_file));
}

TEST_F(ProtobufV2ApiUtilityTest, DEPRECATED_FEATURE_TEST(LoadBinaryV2ProtoFromFile)) {
  // Allow the use of v2.Bootstrap.runtime.
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.deprecated_features:envoy.config.bootstrap.v2.Bootstrap.runtime", "True "}});
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  bootstrap.mutable_runtime()->set_symlink_root("/");

  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb", bootstrap.SerializeAsString());

  envoy::config::bootstrap::v3::Bootstrap proto_from_file;
  TestUtility::loadFromFile(filename, proto_from_file, *api_);
  EXPECT_EQ("/", proto_from_file.hidden_envoy_deprecated_runtime().symlink_root());
  EXPECT_GT(runtime_deprecated_feature_use_.value(), 0);
}

// Verify that a config with a deprecated field can be loaded with runtime global override.
TEST_F(ProtobufUtilityTest, DEPRECATED_FEATURE_TEST(LoadBinaryV2GlobalOverrideProtoFromFile)) {
  // Allow the use of v2.Bootstrap.runtime.
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.features.enable_all_deprecated_features", "true"}});
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  bootstrap.mutable_runtime()->set_symlink_root("/");

  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb", bootstrap.SerializeAsString());

  envoy::config::bootstrap::v3::Bootstrap proto_from_file;
  TestUtility::loadFromFile(filename, proto_from_file, *api_);
  EXPECT_EQ("/", proto_from_file.hidden_envoy_deprecated_runtime().symlink_root());
  EXPECT_GT(runtime_deprecated_feature_use_.value(), 0);
}

// An unknown field (or with wrong type) in a message is rejected.
TEST_F(ProtobufUtilityTest, LoadBinaryProtoUnknownFieldFromFile) {
  ProtobufWkt::Duration source_duration;
  source_duration.set_seconds(42);
  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb", source_duration.SerializeAsString());
  // Verify without boosting
  envoy::config::bootstrap::v3::Bootstrap proto_from_file;
  EXPECT_THROW_WITH_MESSAGE(TestUtility::loadFromFile(filename, proto_from_file, *api_, false),
                            EnvoyException,
                            "Protobuf message (type envoy.config.bootstrap.v3.Bootstrap with "
                            "unknown field set {1}) has unknown fields");

  // Verify with boosting
  EXPECT_THROW_WITH_MESSAGE(TestUtility::loadFromFile(filename, proto_from_file, *api_, true),
                            EnvoyException,
                            "Protobuf message (type envoy.config.bootstrap.v3.Bootstrap with "
                            "unknown field set {1}) has unknown fields");
}

// Multiple unknown fields (or with wrong type) in a message are rejected.
TEST_F(ProtobufUtilityTest, LoadBinaryProtoUnknownMultipleFieldsFromFile) {
  ProtobufWkt::Duration source_duration;
  source_duration.set_seconds(42);
  source_duration.set_nanos(42);
  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb", source_duration.SerializeAsString());
  envoy::config::bootstrap::v3::Bootstrap proto_from_file;
  EXPECT_THROW_WITH_MESSAGE(TestUtility::loadFromFile(filename, proto_from_file, *api_),
                            EnvoyException,
                            "Protobuf message (type envoy.config.bootstrap.v3.Bootstrap with "
                            "unknown field set {1, 2}) has unknown fields");
}

TEST_F(ProtobufUtilityTest, LoadTextProtoFromFile) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  bootstrap.mutable_cluster_manager()
      ->mutable_upstream_bind_config()
      ->mutable_source_address()
      ->set_address("1.1.1.1");

  std::string bootstrap_text;
  ASSERT_TRUE(Protobuf::TextFormat::PrintToString(bootstrap, &bootstrap_text));
  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb_text", bootstrap_text);

  envoy::config::bootstrap::v3::Bootstrap proto_from_file;
  TestUtility::loadFromFile(filename, proto_from_file, *api_);
  EXPECT_EQ(0, runtime_deprecated_feature_use_.value());
  EXPECT_TRUE(TestUtility::protoEqual(bootstrap, proto_from_file));
}

TEST_F(ProtobufUtilityTest, LoadJsonFromFileNoBoosting) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  bootstrap.mutable_cluster_manager()
      ->mutable_upstream_bind_config()
      ->mutable_source_address()
      ->set_address("1.1.1.1");

  std::string bootstrap_text;
  ASSERT_TRUE(Protobuf::TextFormat::PrintToString(bootstrap, &bootstrap_text));
  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb_text", bootstrap_text);

  envoy::config::bootstrap::v3::Bootstrap proto_from_file;
  TestUtility::loadFromFile(filename, proto_from_file, *api_);
  EXPECT_EQ(0, runtime_deprecated_feature_use_.value());
  EXPECT_TRUE(TestUtility::protoEqual(bootstrap, proto_from_file));
}

TEST_F(ProtobufV2ApiUtilityTest, DEPRECATED_FEATURE_TEST(LoadV2TextProtoFromFile)) {
  API_NO_BOOST(envoy::config::bootstrap::v2::Bootstrap) bootstrap;
  bootstrap.mutable_node()->set_build_version("foo");

  std::string bootstrap_text;
  ASSERT_TRUE(Protobuf::TextFormat::PrintToString(bootstrap, &bootstrap_text));
  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb_text", bootstrap_text);

  API_NO_BOOST(envoy::config::bootstrap::v3::Bootstrap) proto_from_file;
  TestUtility::loadFromFile(filename, proto_from_file, *api_);
  EXPECT_GT(runtime_deprecated_feature_use_.value(), 0);
  EXPECT_EQ("foo", proto_from_file.node().hidden_envoy_deprecated_build_version());
}

TEST_F(ProtobufUtilityTest, LoadTextProtoFromFile_Failure) {
  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb_text", "invalid {");

  envoy::config::bootstrap::v3::Bootstrap proto_from_file;
  EXPECT_THROW_WITH_MESSAGE(TestUtility::loadFromFile(filename, proto_from_file, *api_),
                            EnvoyException,
                            "Unable to parse file \"" + filename +
                                "\" as a text protobuf (type envoy.config.bootstrap.v3.Bootstrap)");
}

// String fields annotated as sensitive should be converted to the string "[redacted]". String
// fields that are neither annotated as sensitive nor contained in a sensitive message should be
// left alone.
TEST_F(ProtobufUtilityTest, RedactString) {
  envoy::test::Sensitive actual, expected;
  TestUtility::loadFromYaml(R"EOF(
sensitive_string: This field should be redacted.
sensitive_repeated_string:
  - This field should be redacted (1 of 2).
  - This field should be redacted (2 of 2).
insensitive_string: This field should not be redacted.
insensitive_repeated_string:
  - This field should not be redacted (1 of 2).
  - This field should not be redacted (2 of 2).
)EOF",
                            actual);

  TestUtility::loadFromYaml(R"EOF(
sensitive_string: '[redacted]'
sensitive_repeated_string:
  - '[redacted]'
  - '[redacted]'
insensitive_string: This field should not be redacted.
insensitive_repeated_string:
  - This field should not be redacted (1 of 2).
  - This field should not be redacted (2 of 2).
)EOF",
                            expected);

  MessageUtil::redact(actual);
  EXPECT_TRUE(TestUtility::protoEqual(expected, actual));
}

// Bytes fields annotated as sensitive should be converted to the ASCII / UTF-8 encoding of the
// string "[redacted]". Bytes fields that are neither annotated as sensitive nor contained in a
// sensitive message should be left alone.
TEST_F(ProtobufUtilityTest, RedactBytes) {
  envoy::test::Sensitive actual, expected;
  TestUtility::loadFromYaml(R"EOF(
sensitive_bytes: VGhlc2UgYnl0ZXMgc2hvdWxkIGJlIHJlZGFjdGVkLg==
sensitive_repeated_bytes:
  - VGhlc2UgYnl0ZXMgc2hvdWxkIGJlIHJlZGFjdGVkICgxIG9mIDIpLg==
  - VGhlc2UgYnl0ZXMgc2hvdWxkIGJlIHJlZGFjdGVkICgyIG9mIDIpLg==
insensitive_bytes: VGhlc2UgYnl0ZXMgc2hvdWxkIG5vdCBiZSByZWRhY3RlZC4=
insensitive_repeated_bytes:
  - VGhlc2UgYnl0ZXMgc2hvdWxkIG5vdCBiZSByZWRhY3RlZCAoMSBvZiAyKS4=
  - VGhlc2UgYnl0ZXMgc2hvdWxkIG5vdCBiZSByZWRhY3RlZCAoMiBvZiAyKS4=
)EOF",
                            actual);

  TestUtility::loadFromYaml(R"EOF(
sensitive_bytes: W3JlZGFjdGVkXQ==
sensitive_repeated_bytes:
  - W3JlZGFjdGVkXQ==
  - W3JlZGFjdGVkXQ==
insensitive_bytes: VGhlc2UgYnl0ZXMgc2hvdWxkIG5vdCBiZSByZWRhY3RlZC4=
insensitive_repeated_bytes:
  - VGhlc2UgYnl0ZXMgc2hvdWxkIG5vdCBiZSByZWRhY3RlZCAoMSBvZiAyKS4=
  - VGhlc2UgYnl0ZXMgc2hvdWxkIG5vdCBiZSByZWRhY3RlZCAoMiBvZiAyKS4=
)EOF",
                            expected);

  MessageUtil::redact(actual);
  EXPECT_TRUE(TestUtility::protoEqual(expected, actual));
}

// Ints annotated as sensitive should be cleared. Ints that are neither annotated as sensitive nor
// contained in a sensitive message should be left alone. Note that the same logic should apply to
// any primitive type other than strings and bytes, although we omit tests for that here.
TEST_F(ProtobufUtilityTest, RedactInts) {
  envoy::test::Sensitive actual, expected;
  TestUtility::loadFromYaml(R"EOF(
sensitive_int: 1
sensitive_repeated_int:
  - 1
  - 2
insensitive_int: 1
insensitive_repeated_int:
  - 1
  - 2
)EOF",
                            actual);

  TestUtility::loadFromYaml(R"EOF(
insensitive_int: 1
insensitive_repeated_int:
  - 1
  - 2
)EOF",
                            expected);

  MessageUtil::redact(actual);
  EXPECT_TRUE(TestUtility::protoEqual(expected, actual));
}

// Messages annotated as sensitive should have all their fields redacted recursively. Messages that
// are neither annotated as sensitive nor contained in a sensitive message should be left alone.
TEST_F(ProtobufUtilityTest, RedactMessage) {
  envoy::test::Sensitive actual, expected;
  TestUtility::loadFromYaml(R"EOF(
sensitive_message:
  insensitive_string: This field should be redacted because of its parent.
  insensitive_repeated_string:
    - This field should be redacted because of its parent (1 of 2).
    - This field should be redacted because of its parent (2 of 2).
  insensitive_int: 1
  insensitive_repeated_int:
    - 1
    - 2
sensitive_repeated_message:
  - insensitive_string: This field should be redacted because of its parent (1 of 2).
    insensitive_repeated_string:
      - This field should be redacted because of its parent (1 of 4).
      - This field should be redacted because of its parent (2 of 4).
    insensitive_int: 1
    insensitive_repeated_int:
      - 1
      - 2
  - insensitive_string: This field should be redacted because of its parent (2 of 2).
    insensitive_repeated_string:
      - This field should be redacted because of its parent (3 of 4).
      - This field should be redacted because of its parent (4 of 4).
    insensitive_int: 2
    insensitive_repeated_int:
      - 3
      - 4
insensitive_message:
  insensitive_string: This field should not be redacted.
  insensitive_repeated_string:
    - This field should not be redacted (1 of 2).
    - This field should not be redacted (2 of 2).
  insensitive_int: 1
  insensitive_repeated_int:
    - 1
    - 2
insensitive_repeated_message:
  - insensitive_string: This field should not be redacted (1 of 2).
    insensitive_repeated_string:
      - This field should not be redacted (1 of 4).
      - This field should not be redacted (2 of 4).
    insensitive_int: 1
    insensitive_repeated_int:
      - 1
      - 2
  - insensitive_string: This field should not be redacted (2 of 2).
    insensitive_repeated_string:
      - This field should not be redacted (3 of 4).
      - This field should not be redacted (4 of 4).
    insensitive_int: 2
    insensitive_repeated_int:
      - 3
      - 4
)EOF",
                            actual);

  TestUtility::loadFromYaml(R"EOF(
sensitive_message:
  insensitive_string: '[redacted]'
  insensitive_repeated_string:
    - '[redacted]'
    - '[redacted]'
sensitive_repeated_message:
  - insensitive_string: '[redacted]'
    insensitive_repeated_string:
      - '[redacted]'
      - '[redacted]'
  - insensitive_string: '[redacted]'
    insensitive_repeated_string:
      - '[redacted]'
      - '[redacted]'
insensitive_message:
  insensitive_string: This field should not be redacted.
  insensitive_repeated_string:
    - This field should not be redacted (1 of 2).
    - This field should not be redacted (2 of 2).
  insensitive_int: 1
  insensitive_repeated_int:
    - 1
    - 2
insensitive_repeated_message:
  - insensitive_string: This field should not be redacted (1 of 2).
    insensitive_repeated_string:
      - This field should not be redacted (1 of 4).
      - This field should not be redacted (2 of 4).
    insensitive_int: 1
    insensitive_repeated_int:
      - 1
      - 2
  - insensitive_string: This field should not be redacted (2 of 2).
    insensitive_repeated_string:
      - This field should not be redacted (3 of 4).
      - This field should not be redacted (4 of 4).
    insensitive_int: 2
    insensitive_repeated_int:
      - 3
      - 4
)EOF",
                            expected);

  MessageUtil::redact(actual);
  EXPECT_TRUE(TestUtility::protoEqual(expected, actual));
}

// Messages packed into `Any` should be treated the same as normal messages.
TEST_F(ProtobufUtilityTest, RedactAny) {
  envoy::test::Sensitive actual, expected;
  TestUtility::loadFromYaml(R"EOF(
sensitive_any:
  '@type': type.googleapis.com/envoy.test.Sensitive
  insensitive_string: This field should be redacted because of its parent.
  insensitive_repeated_string:
    - This field should be redacted because of its parent (1 of 2).
    - This field should be redacted because of its parent (2 of 2).
  insensitive_int: 1
  insensitive_repeated_int:
    - 1
    - 2
sensitive_repeated_any:
  - '@type': type.googleapis.com/envoy.test.Sensitive
    insensitive_string: This field should be redacted because of its parent (1 of 2).
    insensitive_repeated_string:
      - This field should be redacted because of its parent (1 of 4).
      - This field should be redacted because of its parent (2 of 4).
    insensitive_int: 1
    insensitive_repeated_int:
      - 1
      - 2
  - '@type': type.googleapis.com/envoy.test.Sensitive
    insensitive_string: This field should be redacted because of its parent (2 of 2).
    insensitive_repeated_string:
      - This field should be redacted because of its parent (3 of 4).
      - This field should be redacted because of its parent (4 of 4).
    insensitive_int: 2
    insensitive_repeated_int:
      - 3
      - 4
insensitive_any:
  '@type': type.googleapis.com/envoy.test.Sensitive
  sensitive_string: This field should be redacted.
  sensitive_repeated_string:
    - This field should be redacted (1 of 2).
    - This field should be redacted (2 of 2).
  sensitive_int: 1
  sensitive_repeated_int:
    - 1
    - 2
  insensitive_string: This field should not be redacted.
  insensitive_repeated_string:
    - This field should not be redacted (1 of 2).
    - This field should not be redacted (2 of 2).
  insensitive_int: 1
  insensitive_repeated_int:
    - 1
    - 2
insensitive_repeated_any:
  - '@type': type.googleapis.com/envoy.test.Sensitive
    sensitive_string: This field should be redacted (1 of 2).
    sensitive_repeated_string:
      - This field should be redacted (1 of 4).
      - This field should be redacted (2 of 4).
    sensitive_int: 1
    sensitive_repeated_int:
      - 1
      - 2
    insensitive_string: This field should not be redacted.
    insensitive_repeated_string:
      - This field should not be redacted (1 of 4).
      - This field should not be redacted (2 of 4).
    insensitive_int: 1
    insensitive_repeated_int:
      - 1
      - 2
  - '@type': type.googleapis.com/envoy.test.Sensitive
    sensitive_string: This field should be redacted (2 of 2).
    sensitive_repeated_string:
      - This field should be redacted (3 of 4).
      - This field should be redacted (4 of 4).
    sensitive_int: 2
    sensitive_repeated_int:
      - 3
      - 4
    insensitive_string: This field should not be redacted.
    insensitive_repeated_string:
      - This field should not be redacted (3 of 4).
      - This field should not be redacted (4 of 4).
    insensitive_int: 2
    insensitive_repeated_int:
      - 3
      - 4
)EOF",
                            actual);

  TestUtility::loadFromYaml(R"EOF(
sensitive_any:
  '@type': type.googleapis.com/envoy.test.Sensitive
  insensitive_string: '[redacted]'
  insensitive_repeated_string:
    - '[redacted]'
    - '[redacted]'
sensitive_repeated_any:
  - '@type': type.googleapis.com/envoy.test.Sensitive
    insensitive_string: '[redacted]'
    insensitive_repeated_string:
      - '[redacted]'
      - '[redacted]'
  - '@type': type.googleapis.com/envoy.test.Sensitive
    insensitive_string: '[redacted]'
    insensitive_repeated_string:
      - '[redacted]'
      - '[redacted]'
insensitive_any:
  '@type': type.googleapis.com/envoy.test.Sensitive
  sensitive_string: '[redacted]'
  sensitive_repeated_string:
    - '[redacted]'
    - '[redacted]'
  insensitive_string: This field should not be redacted.
  insensitive_repeated_string:
    - This field should not be redacted (1 of 2).
    - This field should not be redacted (2 of 2).
  insensitive_int: 1
  insensitive_repeated_int:
    - 1
    - 2
insensitive_repeated_any:
  - '@type': type.googleapis.com/envoy.test.Sensitive
    sensitive_string: '[redacted]'
    sensitive_repeated_string:
      - '[redacted]'
      - '[redacted]'
    insensitive_string: This field should not be redacted.
    insensitive_repeated_string:
      - This field should not be redacted (1 of 4).
      - This field should not be redacted (2 of 4).
    insensitive_int: 1
    insensitive_repeated_int:
      - 1
      - 2
  - '@type': type.googleapis.com/envoy.test.Sensitive
    sensitive_string: '[redacted]'
    sensitive_repeated_string:
      - '[redacted]'
      - '[redacted]'
    insensitive_string: This field should not be redacted.
    insensitive_repeated_string:
      - This field should not be redacted (3 of 4).
      - This field should not be redacted (4 of 4).
    insensitive_int: 2
    insensitive_repeated_int:
      - 3
      - 4
)EOF",
                            expected);

  MessageUtil::redact(actual);
  EXPECT_TRUE(TestUtility::protoEqual(expected, actual));
}

// Empty `Any` can be trivially redacted.
TEST_F(ProtobufUtilityTest, RedactEmptyAny) {
  ProtobufWkt::Any actual;
  TestUtility::loadFromYaml(R"EOF(
'@type': type.googleapis.com/envoy.test.Sensitive
)EOF",
                            actual);

  ProtobufWkt::Any expected = actual;
  MessageUtil::redact(actual);
  EXPECT_TRUE(TestUtility::protoEqual(expected, actual));
}

// Messages packed into `Any` with unknown type URLs are skipped.
TEST_F(ProtobufUtilityTest, RedactAnyWithUnknownTypeUrl) {
  ProtobufWkt::Any actual;
  // Note, `loadFromYaml` validates the type when populating `Any`, so we have to pass the real type
  // first and substitute an unknown message type after loading.
  TestUtility::loadFromYaml(R"EOF(
'@type': type.googleapis.com/envoy.test.Sensitive
sensitive_string: This field is sensitive, but we have no way of knowing.
)EOF",
                            actual);
  actual.set_type_url("type.googleapis.com/envoy.unknown.Message");

  ProtobufWkt::Any expected = actual;
  MessageUtil::redact(actual);
  EXPECT_TRUE(TestUtility::protoEqual(expected, actual));
}

// Messages packed into `TypedStruct` should be treated the same as normal messages. Note that
// ints are quoted as strings here because that's what happens in the JSON conversion.
TEST_F(ProtobufUtilityTest, RedactTypedStruct) {
  envoy::test::Sensitive actual, expected;
  TestUtility::loadFromYaml(R"EOF(
sensitive_typed_struct:
  type_url: type.googleapis.com/envoy.test.Sensitive
  value:
    insensitive_string: This field should be redacted because of its parent.
    insensitive_repeated_string:
      - This field should be redacted because of its parent (1 of 2).
      - This field should be redacted because of its parent (2 of 2).
    insensitive_int: '1'
    insensitive_repeated_int:
      - '1'
      - '2'
sensitive_repeated_typed_struct:
  - type_url: type.googleapis.com/envoy.test.Sensitive
    value:
      insensitive_string: This field should be redacted because of its parent (1 of 2).
      insensitive_repeated_string:
        - This field should be redacted because of its parent (1 of 4).
        - This field should be redacted because of its parent (2 of 4).
      insensitive_int: '1'
      insensitive_repeated_int:
        - '1'
        - '2'
  - type_url: type.googleapis.com/envoy.test.Sensitive
    value:
      insensitive_string: This field should be redacted because of its parent (2 of 2).
      insensitive_repeated_string:
        - This field should be redacted because of its parent (3 of 4).
        - This field should be redacted because of its parent (4 of 4).
      insensitive_int: '2'
      insensitive_repeated_int:
        - '3'
        - '4'
insensitive_typed_struct:
  type_url: type.googleapis.com/envoy.test.Sensitive
  value:
    sensitive_string: This field should be redacted.
    sensitive_repeated_string:
      - This field should be redacted (1 of 2).
      - This field should be redacted (2 of 2).
    sensitive_int: '1'
    sensitive_repeated_int:
      - '1'
      - '2'
    insensitive_string: This field should not be redacted.
    insensitive_repeated_string:
      - This field should not be redacted (1 of 2).
      - This field should not be redacted (2 of 2).
    insensitive_int: '1'
    insensitive_repeated_int:
      - '1'
      - '2'
insensitive_repeated_typed_struct:
  - type_url: type.googleapis.com/envoy.test.Sensitive
    value:
      sensitive_string: This field should be redacted (1 of 2).
      sensitive_repeated_string:
        - This field should be redacted (1 of 4).
        - This field should be redacted (2 of 4).
      sensitive_int: '1'
      sensitive_repeated_int:
        - '1'
        - '2'
      insensitive_string: This field should not be redacted.
      insensitive_repeated_string:
        - This field should not be redacted (1 of 4).
        - This field should not be redacted (2 of 4).
      insensitive_int: '1'
      insensitive_repeated_int:
        - '1'
        - '2'
  - type_url: type.googleapis.com/envoy.test.Sensitive
    value:
      sensitive_string: This field should be redacted (2 of 2).
      sensitive_repeated_string:
        - This field should be redacted (3 of 4).
        - This field should be redacted (4 of 4).
      sensitive_int: '2'
      sensitive_repeated_int:
        - '3'
        - '4'
      insensitive_string: This field should not be redacted.
      insensitive_repeated_string:
        - This field should not be redacted (3 of 4).
        - This field should not be redacted (4 of 4).
      insensitive_int: '2'
      insensitive_repeated_int:
        - '3'
        - '4'
)EOF",
                            actual);

  TestUtility::loadFromYaml(R"EOF(
sensitive_typed_struct:
  type_url: type.googleapis.com/envoy.test.Sensitive
  value:
    insensitive_string: '[redacted]'
    insensitive_repeated_string:
      - '[redacted]'
      - '[redacted]'
sensitive_repeated_typed_struct:
  - type_url: type.googleapis.com/envoy.test.Sensitive
    value:
      insensitive_string: '[redacted]'
      insensitive_repeated_string:
        - '[redacted]'
        - '[redacted]'
  - type_url: type.googleapis.com/envoy.test.Sensitive
    value:
      insensitive_string: '[redacted]'
      insensitive_repeated_string:
        - '[redacted]'
        - '[redacted]'
insensitive_typed_struct:
  type_url: type.googleapis.com/envoy.test.Sensitive
  value:
    sensitive_string: '[redacted]'
    sensitive_repeated_string:
      - '[redacted]'
      - '[redacted]'
    insensitive_string: This field should not be redacted.
    insensitive_repeated_string:
      - This field should not be redacted (1 of 2).
      - This field should not be redacted (2 of 2).
    insensitive_int: '1'
    insensitive_repeated_int:
      - '1'
      - '2'
insensitive_repeated_typed_struct:
  - type_url: type.googleapis.com/envoy.test.Sensitive
    value:
      sensitive_string: '[redacted]'
      sensitive_repeated_string:
        - '[redacted]'
        - '[redacted]'
      insensitive_string: This field should not be redacted.
      insensitive_repeated_string:
        - This field should not be redacted (1 of 4).
        - This field should not be redacted (2 of 4).
      insensitive_int: '1'
      insensitive_repeated_int:
        - '1'
        - '2'
  - type_url: type.googleapis.com/envoy.test.Sensitive
    value:
      sensitive_string: '[redacted]'
      sensitive_repeated_string:
        - '[redacted]'
        - '[redacted]'
      insensitive_string: This field should not be redacted.
      insensitive_repeated_string:
        - This field should not be redacted (3 of 4).
        - This field should not be redacted (4 of 4).
      insensitive_int: '2'
      insensitive_repeated_int:
        - '3'
        - '4'
)EOF",
                            expected);

  MessageUtil::redact(actual);
  EXPECT_TRUE(TestUtility::protoEqual(expected, actual));
}

// Empty `TypedStruct` can be trivially redacted.
TEST_F(ProtobufUtilityTest, RedactEmptyTypedStruct) {
  udpa::type::v1::TypedStruct actual;
  TestUtility::loadFromYaml(R"EOF(
type_url: type.googleapis.com/envoy.test.Sensitive
)EOF",
                            actual);

  udpa::type::v1::TypedStruct expected = actual;
  MessageUtil::redact(actual);
  EXPECT_TRUE(TestUtility::protoEqual(expected, actual));
}

// Messages packed into `TypedStruct` with unknown type URLs are skipped.
TEST_F(ProtobufUtilityTest, RedactTypedStructWithUnknownTypeUrl) {
  udpa::type::v1::TypedStruct actual;
  TestUtility::loadFromYaml(R"EOF(
type_url: type.googleapis.com/envoy.unknown.Message
value:
  sensitive_string: This field is sensitive, but we have no way of knowing.
)EOF",
                            actual);

  udpa::type::v1::TypedStruct expected = actual;
  MessageUtil::redact(actual);
  EXPECT_TRUE(TestUtility::protoEqual(expected, actual));
}

// Deeply-nested opaque protos (`Any` and `TypedStruct`), which are reified using the
// `DynamicMessageFactory`, should be redacted correctly.
TEST_F(ProtobufUtilityTest, RedactDeeplyNestedOpaqueProtos) {
  envoy::test::Sensitive actual, expected;
  TestUtility::loadFromYaml(R"EOF(
insensitive_any:
  '@type': type.googleapis.com/envoy.test.Sensitive
  insensitive_any:
    '@type': type.googleapis.com/envoy.test.Sensitive
    sensitive_string: This field should be redacted (1 of 4).
  insensitive_typed_struct:
    type_url: type.googleapis.com/envoy.test.Sensitive
    value:
      sensitive_string: This field should be redacted (2 of 4).
insensitive_typed_struct:
  type_url: type.googleapis.com/envoy.test.Sensitive
  value:
    insensitive_any:
      '@type': type.googleapis.com/envoy.test.Sensitive
      sensitive_string: This field should be redacted (3 of 4).
    insensitive_typed_struct:
      type_url: type.googleapis.com/envoy.test.Sensitive
      value:
        sensitive_string: This field should be redacted (4 of 4).
)EOF",
                            actual);
  TestUtility::loadFromYaml(R"EOF(
insensitive_any:
  '@type': type.googleapis.com/envoy.test.Sensitive
  insensitive_any:
    '@type': type.googleapis.com/envoy.test.Sensitive
    sensitive_string: '[redacted]'
  insensitive_typed_struct:
    type_url: type.googleapis.com/envoy.test.Sensitive
    value:
      sensitive_string: '[redacted]'
insensitive_typed_struct:
  type_url: type.googleapis.com/envoy.test.Sensitive
  value:
    insensitive_any:
      '@type': type.googleapis.com/envoy.test.Sensitive
      sensitive_string: '[redacted]'
    insensitive_typed_struct:
      type_url: type.googleapis.com/envoy.test.Sensitive
      value:
        sensitive_string: '[redacted]'
)EOF",
                            expected);
  MessageUtil::redact(actual);
  EXPECT_TRUE(TestUtility::protoEqual(expected, actual));
}

TEST_F(ProtobufUtilityTest, KeyValueStruct) {
  const ProtobufWkt::Struct obj = MessageUtil::keyValueStruct("test_key", "test_value");
  EXPECT_EQ(obj.fields_size(), 1);
  EXPECT_EQ(obj.fields().at("test_key").kind_case(), ProtobufWkt::Value::KindCase::kStringValue);
  EXPECT_EQ(obj.fields().at("test_key").string_value(), "test_value");
}

TEST_F(ProtobufUtilityTest, KeyValueStructMap) {
  const ProtobufWkt::Struct obj = MessageUtil::keyValueStruct(
      {{"test_key", "test_value"}, {"test_another_key", "test_another_value"}});
  EXPECT_EQ(obj.fields_size(), 2);
  EXPECT_EQ(obj.fields().at("test_key").kind_case(), ProtobufWkt::Value::KindCase::kStringValue);
  EXPECT_EQ(obj.fields().at("test_key").string_value(), "test_value");
  EXPECT_EQ(obj.fields().at("test_another_key").kind_case(),
            ProtobufWkt::Value::KindCase::kStringValue);
  EXPECT_EQ(obj.fields().at("test_another_key").string_value(), "test_another_value");
}

TEST_F(ProtobufUtilityTest, ValueUtilEqual_NullValues) {
  ProtobufWkt::Value v1, v2;
  v1.set_null_value(ProtobufWkt::NULL_VALUE);
  v2.set_null_value(ProtobufWkt::NULL_VALUE);

  ProtobufWkt::Value other;
  other.set_string_value("s");

  EXPECT_TRUE(ValueUtil::equal(v1, v2));
  EXPECT_FALSE(ValueUtil::equal(v1, other));
}

TEST_F(ProtobufUtilityTest, ValueUtilEqual_StringValues) {
  ProtobufWkt::Value v1, v2, v3;
  v1.set_string_value("s");
  v2.set_string_value("s");
  v3.set_string_value("not_s");

  EXPECT_TRUE(ValueUtil::equal(v1, v2));
  EXPECT_FALSE(ValueUtil::equal(v1, v3));
}

TEST_F(ProtobufUtilityTest, ValueUtilEqual_NumberValues) {
  ProtobufWkt::Value v1, v2, v3;
  v1.set_number_value(1.0);
  v2.set_number_value(1.0);
  v3.set_number_value(100.0);

  EXPECT_TRUE(ValueUtil::equal(v1, v2));
  EXPECT_FALSE(ValueUtil::equal(v1, v3));
}

TEST_F(ProtobufUtilityTest, ValueUtilEqual_BoolValues) {
  ProtobufWkt::Value v1, v2, v3;
  v1.set_bool_value(true);
  v2.set_bool_value(true);
  v3.set_bool_value(false);

  EXPECT_TRUE(ValueUtil::equal(v1, v2));
  EXPECT_FALSE(ValueUtil::equal(v1, v3));
}

TEST_F(ProtobufUtilityTest, ValueUtilEqual_StructValues) {
  ProtobufWkt::Value string_val1, string_val2, bool_val;

  string_val1.set_string_value("s1");
  string_val2.set_string_value("s2");
  bool_val.set_bool_value(true);

  ProtobufWkt::Value v1, v2, v3, v4;
  v1.mutable_struct_value()->mutable_fields()->insert({"f1", string_val1});
  v1.mutable_struct_value()->mutable_fields()->insert({"f2", bool_val});

  v2.mutable_struct_value()->mutable_fields()->insert({"f1", string_val1});
  v2.mutable_struct_value()->mutable_fields()->insert({"f2", bool_val});

  v3.mutable_struct_value()->mutable_fields()->insert({"f1", string_val2});
  v3.mutable_struct_value()->mutable_fields()->insert({"f2", bool_val});

  v4.mutable_struct_value()->mutable_fields()->insert({"f1", string_val1});

  EXPECT_TRUE(ValueUtil::equal(v1, v2));
  EXPECT_FALSE(ValueUtil::equal(v1, v3));
  EXPECT_FALSE(ValueUtil::equal(v1, v4));
}

TEST_F(ProtobufUtilityTest, ValueUtilEqual_ListValues) {
  ProtobufWkt::Value v1, v2, v3, v4;
  v1.mutable_list_value()->add_values()->set_string_value("s");
  v1.mutable_list_value()->add_values()->set_bool_value(true);

  v2.mutable_list_value()->add_values()->set_string_value("s");
  v2.mutable_list_value()->add_values()->set_bool_value(true);

  v3.mutable_list_value()->add_values()->set_bool_value(true);
  v3.mutable_list_value()->add_values()->set_string_value("s");

  v4.mutable_list_value()->add_values()->set_string_value("s");

  EXPECT_TRUE(ValueUtil::equal(v1, v2));
  EXPECT_FALSE(ValueUtil::equal(v1, v3));
  EXPECT_FALSE(ValueUtil::equal(v1, v4));
}

TEST_F(ProtobufUtilityTest, ValueUtilHash) {
  ProtobufWkt::Value v;
  v.set_string_value("s1");

  EXPECT_NE(ValueUtil::hash(v), 0);
}

TEST_F(ProtobufUtilityTest, MessageUtilLoadYamlDouble) {
  ProtobufWkt::DoubleValue v;
  MessageUtil::loadFromYaml("value: 1.0", v, ProtobufMessage::getNullValidationVisitor());
  EXPECT_DOUBLE_EQ(1.0, v.value());
}

TEST_F(ProtobufUtilityTest, ValueUtilLoadFromYamlScalar) {
  EXPECT_EQ(ValueUtil::loadFromYaml("null").ShortDebugString(), "null_value: NULL_VALUE");
  EXPECT_EQ(ValueUtil::loadFromYaml("true").ShortDebugString(), "bool_value: true");
  EXPECT_EQ(ValueUtil::loadFromYaml("1").ShortDebugString(), "number_value: 1");
  EXPECT_EQ(ValueUtil::loadFromYaml("9223372036854775807").ShortDebugString(),
            "string_value: \"9223372036854775807\"");
  EXPECT_EQ(ValueUtil::loadFromYaml("\"foo\"").ShortDebugString(), "string_value: \"foo\"");
  EXPECT_EQ(ValueUtil::loadFromYaml("foo").ShortDebugString(), "string_value: \"foo\"");
}

TEST_F(ProtobufUtilityTest, ValueUtilLoadFromYamlObject) {
  EXPECT_EQ(ValueUtil::loadFromYaml("[foo, bar]").ShortDebugString(),
            "list_value { values { string_value: \"foo\" } values { string_value: \"bar\" } }");
  EXPECT_EQ(ValueUtil::loadFromYaml("foo: bar").ShortDebugString(),
            "struct_value { fields { key: \"foo\" value { string_value: \"bar\" } } }");
}

TEST_F(ProtobufUtilityTest, ValueUtilLoadFromYamlException) {
  std::string bad_yaml = R"EOF(
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: {{ ntop_ip_loopback_address }}
      port_value: 0
)EOF";

  EXPECT_THROW_WITH_REGEX(ValueUtil::loadFromYaml(bad_yaml), EnvoyException, "bad conversion");
  EXPECT_THROW_WITHOUT_REGEX(ValueUtil::loadFromYaml(bad_yaml), EnvoyException,
                             "Unexpected YAML exception");
}

TEST_F(ProtobufUtilityTest, HashedValue) {
  ProtobufWkt::Value v1, v2, v3;
  v1.set_string_value("s");
  v2.set_string_value("s");
  v3.set_string_value("not_s");

  HashedValue hv1(v1), hv2(v2), hv3(v3);

  EXPECT_EQ(hv1, hv2);
  EXPECT_NE(hv1, hv3);

  HashedValue copy(hv1); // NOLINT(performance-unnecessary-copy-initialization)
  EXPECT_EQ(hv1, copy);
}

TEST_F(ProtobufUtilityTest, HashedValueStdHash) {
  ProtobufWkt::Value v1, v2, v3;
  v1.set_string_value("s");
  v2.set_string_value("s");
  v3.set_string_value("not_s");

  HashedValue hv1(v1), hv2(v2), hv3(v3);

  absl::node_hash_set<HashedValue> set;
  set.emplace(hv1);
  set.emplace(hv2);
  set.emplace(hv3);

  EXPECT_EQ(set.size(), 2); // hv1 == hv2
  EXPECT_NE(set.find(hv1), set.end());
  EXPECT_NE(set.find(hv3), set.end());
}

// MessageUtility::anyConvert() with the wrong type throws.
TEST_F(ProtobufUtilityTest, AnyConvertWrongType) {
  ProtobufWkt::Duration source_duration;
  source_duration.set_seconds(42);
  ProtobufWkt::Any source_any;
  source_any.PackFrom(source_duration);
  EXPECT_THROW_WITH_REGEX(
      TestUtility::anyConvert<ProtobufWkt::Timestamp>(source_any), EnvoyException,
      R"(Unable to unpack as google.protobuf.Timestamp: \[type.googleapis.com/google.protobuf.Duration\] .*)");
}

// Validated exception thrown when anyConvertAndValidate observes a PGV failures.
TEST_F(ProtobufUtilityTest, AnyConvertAndValidateFailedValidation) {
  envoy::config::cluster::v3::Filter filter;
  ProtobufWkt::Any source_any;
  source_any.PackFrom(filter);
  EXPECT_THROW(MessageUtil::anyConvertAndValidate<envoy::config::cluster::v3::Filter>(
                   source_any, ProtobufMessage::getStrictValidationVisitor()),
               ProtoValidationException);
}

// MessageUtility::unpackTo() with the wrong type throws.
TEST_F(ProtobufV2ApiUtilityTest, UnpackToWrongType) {
  ProtobufWkt::Duration source_duration;
  source_duration.set_seconds(42);
  ProtobufWkt::Any source_any;
  source_any.PackFrom(source_duration);
  ProtobufWkt::Timestamp dst;
  EXPECT_THROW_WITH_REGEX(
      MessageUtil::unpackTo(source_any, dst), EnvoyException,
      R"(Unable to unpack as google.protobuf.Timestamp: \[type.googleapis.com/google.protobuf.Duration\] .*)");
}

// MessageUtility::unpackTo() with API message works at same version.
TEST_F(ProtobufUtilityTest, UnpackToSameVersion) {
  {
    API_NO_BOOST(envoy::api::v2::Cluster) source;
    source.set_drain_connections_on_host_removal(true);
    ProtobufWkt::Any source_any;
    source_any.PackFrom(source);
    API_NO_BOOST(envoy::api::v2::Cluster) dst;
    MessageUtil::unpackTo(source_any, dst);
    EXPECT_TRUE(dst.drain_connections_on_host_removal());
  }
  {
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) source;
    source.set_ignore_health_on_host_removal(true);
    ProtobufWkt::Any source_any;
    source_any.PackFrom(source);
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) dst;
    MessageUtil::unpackTo(source_any, dst);
    EXPECT_TRUE(dst.ignore_health_on_host_removal());
  }
}

// MessageUtility::unpackTo() with API message works across version.
TEST_F(ProtobufV2ApiUtilityTest, UnpackToNextVersion) {
  API_NO_BOOST(envoy::api::v2::Cluster) source;
  source.set_drain_connections_on_host_removal(true);
  ProtobufWkt::Any source_any;
  source_any.PackFrom(source);
  API_NO_BOOST(envoy::config::cluster::v3::Cluster) dst;
  MessageUtil::unpackTo(source_any, dst);
  EXPECT_GT(runtime_deprecated_feature_use_.value(), 0);
  EXPECT_TRUE(dst.ignore_health_on_host_removal());
}

// Validate warning messages on v2 upgrades.
TEST_F(ProtobufV2ApiUtilityTest, V2UpgradeWarningLogs) {
  API_NO_BOOST(envoy::config::cluster::v3::Cluster) dst;
  // First attempt works.
  EXPECT_LOG_CONTAINS("warn", "Configuration does not parse cleanly as v3",
                      MessageUtil::loadFromJson("{drain_connections_on_host_removal: true}", dst,
                                                ProtobufMessage::getNullValidationVisitor()));
  // Second attempt immediately after fails.
  EXPECT_LOG_NOT_CONTAINS("warn", "Configuration does not parse cleanly as v3",
                          MessageUtil::loadFromJson("{drain_connections_on_host_removal: true}",
                                                    dst,
                                                    ProtobufMessage::getNullValidationVisitor()));
  // Third attempt works, since this is a different log message.
  EXPECT_LOG_CONTAINS("warn", "Configuration does not parse cleanly as v3",
                      MessageUtil::loadFromJson("{drain_connections_on_host_removal: false}", dst,
                                                ProtobufMessage::getNullValidationVisitor()));
  // This is kind of terrible, but it's hard to do dependency injection at
  // onVersionUpgradeDeprecation().
  std::this_thread::sleep_for(5s); // NOLINT
  // We can log the original warning again.
  EXPECT_LOG_CONTAINS("warn", "Configuration does not parse cleanly as v3",
                      MessageUtil::loadFromJson("{drain_connections_on_host_removal: true}", dst,
                                                ProtobufMessage::getNullValidationVisitor()));
}

// MessageUtility::loadFromJson() throws on garbage JSON.
TEST_F(ProtobufUtilityTest, LoadFromJsonGarbage) {
  envoy::config::cluster::v3::Cluster dst;
  EXPECT_THROW_WITH_REGEX(MessageUtil::loadFromJson("{drain_connections_on_host_removal: true", dst,
                                                    ProtobufMessage::getNullValidationVisitor()),
                          EnvoyException, "Unable to parse JSON as proto.*after key:value pair.");
}

// MessageUtility::loadFromJson() with API message works at same version.
TEST_F(ProtobufUtilityTest, LoadFromJsonSameVersion) {
  {
    API_NO_BOOST(envoy::api::v2::Cluster) dst;
    MessageUtil::loadFromJson("{drain_connections_on_host_removal: true}", dst,
                              ProtobufMessage::getNullValidationVisitor());
    EXPECT_EQ(0, runtime_deprecated_feature_use_.value());
    EXPECT_TRUE(dst.drain_connections_on_host_removal());
  }
  {
    API_NO_BOOST(envoy::api::v2::Cluster) dst;
    MessageUtil::loadFromJson("{drain_connections_on_host_removal: true}", dst,
                              ProtobufMessage::getStrictValidationVisitor());
    EXPECT_EQ(0, runtime_deprecated_feature_use_.value());
    EXPECT_TRUE(dst.drain_connections_on_host_removal());
  }
  {
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) dst;
    MessageUtil::loadFromJson("{ignore_health_on_host_removal: true}", dst,
                              ProtobufMessage::getNullValidationVisitor());
    EXPECT_EQ(0, runtime_deprecated_feature_use_.value());
    EXPECT_TRUE(dst.ignore_health_on_host_removal());
  }
  {
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) dst;
    MessageUtil::loadFromJson("{ignore_health_on_host_removal: true}", dst,
                              ProtobufMessage::getStrictValidationVisitor());
    EXPECT_EQ(0, runtime_deprecated_feature_use_.value());
    EXPECT_TRUE(dst.ignore_health_on_host_removal());
  }
}

// MessageUtility::loadFromJson() avoids boosting when version specified.
TEST_F(ProtobufUtilityTest, LoadFromJsonNoBoosting) {
  envoy::config::cluster::v3::Cluster dst;
  EXPECT_THROW_WITH_REGEX(
      MessageUtil::loadFromJson("{drain_connections_on_host_removal: true}", dst,
                                ProtobufMessage::getStrictValidationVisitor(), false),
      EnvoyException, "INVALID_ARGUMENT:drain_connections_on_host_removal: Cannot find field.");
}

// MessageUtility::loadFromJson() with API message works across version.
TEST_F(ProtobufV2ApiUtilityTest, LoadFromJsonNextVersion) {
  {
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) dst;
    MessageUtil::loadFromJson("{use_tcp_for_dns_lookups: true}", dst,
                              ProtobufMessage::getNullValidationVisitor());
    EXPECT_EQ(0, runtime_deprecated_feature_use_.value());
    EXPECT_TRUE(dst.use_tcp_for_dns_lookups());
  }
  {
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) dst;
    MessageUtil::loadFromJson("{use_tcp_for_dns_lookups: true}", dst,
                              ProtobufMessage::getStrictValidationVisitor());
    EXPECT_EQ(0, runtime_deprecated_feature_use_.value());
    EXPECT_TRUE(dst.use_tcp_for_dns_lookups());
  }
  {
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) dst;
    MessageUtil::loadFromJson("{drain_connections_on_host_removal: true}", dst,
                              ProtobufMessage::getNullValidationVisitor());
    EXPECT_GT(runtime_deprecated_feature_use_.value(), 0);
    EXPECT_TRUE(dst.ignore_health_on_host_removal());
  }
  {
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) dst;
    MessageUtil::loadFromJson("{drain_connections_on_host_removal: true}", dst,
                              ProtobufMessage::getStrictValidationVisitor());
    EXPECT_GT(runtime_deprecated_feature_use_.value(), 0);
    EXPECT_TRUE(dst.ignore_health_on_host_removal());
  }
}

TEST_F(ProtobufUtilityTest, JsonConvertSuccess) {
  envoy::config::bootstrap::v3::Bootstrap source;
  source.set_flags_path("foo");
  ProtobufWkt::Struct tmp;
  envoy::config::bootstrap::v3::Bootstrap dest;
  TestUtility::jsonConvert(source, tmp);
  TestUtility::jsonConvert(tmp, dest);
  EXPECT_EQ("foo", dest.flags_path());
}

TEST_F(ProtobufUtilityTest, JsonConvertUnknownFieldSuccess) {
  const ProtobufWkt::Struct obj = MessageUtil::keyValueStruct("test_key", "test_value");
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  EXPECT_NO_THROW(
      MessageUtil::jsonConvert(obj, ProtobufMessage::getNullValidationVisitor(), bootstrap));
}

TEST_F(ProtobufUtilityTest, JsonConvertFail) {
  ProtobufWkt::Duration source_duration;
  source_duration.set_seconds(-281474976710656);
  ProtobufWkt::Struct dest_struct;
  EXPECT_THROW_WITH_REGEX(TestUtility::jsonConvert(source_duration, dest_struct), EnvoyException,
                          "Unable to convert protobuf message to JSON string.*"
                          "seconds exceeds limit for field:  seconds: -281474976710656\n");
}

// Regression test for https://github.com/envoyproxy/envoy/issues/3665.
TEST_F(ProtobufUtilityTest, JsonConvertCamelSnake) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  // Make sure we use a field eligible for snake/camel case translation.
  bootstrap.mutable_cluster_manager()->set_local_cluster_name("foo");
  ProtobufWkt::Struct json;
  TestUtility::jsonConvert(bootstrap, json);
  // Verify we can round-trip. This didn't cause the #3665 regression, but useful as a sanity check.
  TestUtility::loadFromJson(MessageUtil::getJsonStringFromMessageOrDie(json, false), bootstrap);
  // Verify we don't do a camel case conversion.
  EXPECT_EQ("foo", json.fields()
                       .at("cluster_manager")
                       .struct_value()
                       .fields()
                       .at("local_cluster_name")
                       .string_value());
}

// Test the jsonConvertValue happy path. Failure modes are converted by jsonConvert tests.
TEST_F(ProtobufUtilityTest, JsonConvertValueSuccess) {
  {
    envoy::config::bootstrap::v3::Bootstrap source;
    source.set_flags_path("foo");
    ProtobufWkt::Value tmp;
    envoy::config::bootstrap::v3::Bootstrap dest;
    MessageUtil::jsonConvertValue(source, tmp);
    TestUtility::jsonConvert(tmp, dest);
    EXPECT_EQ("foo", dest.flags_path());
  }

  {
    ProtobufWkt::StringValue source;
    source.set_value("foo");
    ProtobufWkt::Value dest;
    MessageUtil::jsonConvertValue(source, dest);

    ProtobufWkt::Value expected;
    expected.set_string_value("foo");
    EXPECT_THAT(dest, ProtoEq(expected));
  }
}

TEST_F(ProtobufUtilityTest, YamlLoadFromStringFail) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  // Verify loadFromYaml can parse valid YAML string.
  TestUtility::loadFromYaml("node: { id: node1 }", bootstrap);
  // Verify loadFromYaml throws error when the input is an invalid YAML string.
  EXPECT_THROW_WITH_MESSAGE(
      TestUtility::loadFromYaml("not_a_yaml_that_can_be_converted_to_json", bootstrap),
      EnvoyException, "Unable to convert YAML as JSON: not_a_yaml_that_can_be_converted_to_json");
  // When wrongly inputted by a file path, loadFromYaml throws an error.
  EXPECT_THROW_WITH_MESSAGE(TestUtility::loadFromYaml("/home/configs/config.yaml", bootstrap),
                            EnvoyException,
                            "Unable to convert YAML as JSON: /home/configs/config.yaml");
  // Verify loadFromYaml throws error when the input leads to an Array. This error message is
  // arguably more useful than only "Unable to convert YAML as JSON".
  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYaml("- node: { id: node1 }", bootstrap),
                          EnvoyException,
                          "Unable to parse JSON as proto.*Root element must be a message.*");
}

TEST_F(ProtobufUtilityTest, GetFlowYamlStringFromMessage) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  bootstrap.set_flags_path("foo");
  EXPECT_EQ("{flags_path: foo}", MessageUtil::getYamlStringFromMessage(bootstrap, false, false));
}

TEST_F(ProtobufUtilityTest, GetBlockYamlStringFromMessage) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  bootstrap.set_flags_path("foo");
  EXPECT_EQ("flags_path: foo", MessageUtil::getYamlStringFromMessage(bootstrap, true, false));
}

TEST_F(ProtobufUtilityTest, GetBlockYamlStringFromRecursiveMessage) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  bootstrap.set_flags_path("foo");
  bootstrap.mutable_node();
  bootstrap.mutable_static_resources()->add_listeners()->set_name("http");

  const std::string expected_yaml = R"EOF(
node:
  {}
static_resources:
  listeners:
    - name: http
flags_path: foo)EOF";
  EXPECT_EQ(expected_yaml, "\n" + MessageUtil::getYamlStringFromMessage(bootstrap, true, false));
}

TEST_F(ProtobufUtilityTest, GetYamlStringFromProtoInvalidAny) {
  ProtobufWkt::Any source_any;
  source_any.set_type_url("type.googleapis.com/bad.type.url");
  source_any.set_value("asdf");
  EXPECT_THROW(MessageUtil::getYamlStringFromMessage(source_any, true), EnvoyException);
}

TEST(DurationUtilTest, OutOfRange) {
  {
    ProtobufWkt::Duration duration;
    duration.set_seconds(-1);
    EXPECT_THROW(DurationUtil::durationToMilliseconds(duration), DurationUtil::OutOfRangeException);
  }
  {
    ProtobufWkt::Duration duration;
    duration.set_nanos(-1);
    EXPECT_THROW(DurationUtil::durationToMilliseconds(duration), DurationUtil::OutOfRangeException);
  }
  {
    ProtobufWkt::Duration duration;
    duration.set_nanos(1000000000);
    EXPECT_THROW(DurationUtil::durationToMilliseconds(duration), DurationUtil::OutOfRangeException);
  }
  {
    ProtobufWkt::Duration duration;
    duration.set_seconds(Protobuf::util::TimeUtil::kDurationMaxSeconds + 1);
    EXPECT_THROW(DurationUtil::durationToMilliseconds(duration), DurationUtil::OutOfRangeException);
  }
}

class DeprecatedFieldsTest : public testing::TestWithParam<bool>, protected RuntimeStatsHelper {
protected:
  DeprecatedFieldsTest() : with_upgrade_(GetParam()) {}

  void checkForDeprecation(const Protobuf::Message& message) {
    if (with_upgrade_) {
      envoy::test::deprecation_test::UpgradedBase upgraded_message;
      Config::VersionConverter::upgrade(message, upgraded_message);
      MessageUtil::checkForUnexpectedFields(upgraded_message,
                                            ProtobufMessage::getStrictValidationVisitor());
    } else {
      MessageUtil::checkForUnexpectedFields(message, ProtobufMessage::getStrictValidationVisitor());
    }
  }

  const bool with_upgrade_;
};

INSTANTIATE_TEST_SUITE_P(Versions, DeprecatedFieldsTest, testing::Values(false, true));

TEST_P(DeprecatedFieldsTest, NoCrashIfRuntimeMissing) {
  loader_.reset();

  envoy::test::deprecation_test::Base base;
  base.set_not_deprecated("foo");
  // Fatal checks for a non-deprecated field should cause no problem.
  checkForDeprecation(base);
}

TEST_P(DeprecatedFieldsTest, NoErrorWhenDeprecatedFieldsUnused) {
  envoy::test::deprecation_test::Base base;
  base.set_not_deprecated("foo");
  // Fatal checks for a non-deprecated field should cause no problem.
  checkForDeprecation(base);
  EXPECT_EQ(0, runtime_deprecated_feature_use_.value());
  EXPECT_EQ(0, deprecated_feature_seen_since_process_start_.value());
}

TEST_P(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(IndividualFieldDeprecated)) {
  envoy::test::deprecation_test::Base base;
  base.set_is_deprecated("foo");
  // Non-fatal checks for a deprecated field should log rather than throw an exception.
  EXPECT_LOG_CONTAINS("warning",
                      "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated'",
                      checkForDeprecation(base));
  EXPECT_EQ(1, runtime_deprecated_feature_use_.value());
  EXPECT_EQ(1, deprecated_feature_seen_since_process_start_.value());
}

// Use of a deprecated and disallowed field should result in an exception.
TEST_P(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(IndividualFieldDisallowed)) {
  envoy::test::deprecation_test::Base base;
  base.set_is_deprecated_fatal("foo");
  EXPECT_THROW_WITH_REGEX(
      checkForDeprecation(base), Envoy::ProtobufMessage::DeprecatedProtoFieldException,
      "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated_fatal'");
}

TEST_P(DeprecatedFieldsTest,
       DEPRECATED_FEATURE_TEST(IndividualFieldDisallowedWithRuntimeOverride)) {
  envoy::test::deprecation_test::Base base;
  base.set_is_deprecated_fatal("foo");

  // Make sure this is set up right.
  EXPECT_THROW_WITH_REGEX(
      checkForDeprecation(base), Envoy::ProtobufMessage::DeprecatedProtoFieldException,
      "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated_fatal'");
  // The config will be rejected, so the feature will not be used.
  EXPECT_EQ(0, runtime_deprecated_feature_use_.value());

  // Now create a new snapshot with this feature allowed.
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.deprecated_features:envoy.test.deprecation_test.Base.is_deprecated_fatal",
        "True "}});

  // Now the same deprecation check should only trigger a warning.
  EXPECT_LOG_CONTAINS(
      "warning",
      "Using runtime overrides to continue using now fatal-by-default deprecated option "
      "'envoy.test.deprecation_test.Base.is_deprecated_fatal'",
      checkForDeprecation(base));
  EXPECT_EQ(1, runtime_deprecated_feature_use_.value());
}

// Test that a deprecated field is allowed with runtime global override.
TEST_P(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(IndividualFieldDisallowedWithGlobalOverride)) {
  envoy::test::deprecation_test::Base base;
  base.set_is_deprecated_fatal("foo");

  // Make sure this is set up right.
  EXPECT_THROW_WITH_REGEX(
      checkForDeprecation(base), Envoy::ProtobufMessage::DeprecatedProtoFieldException,
      "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated_fatal'");
  // The config will be rejected, so the feature will not be used.
  EXPECT_EQ(0, runtime_deprecated_feature_use_.value());

  // Now create a new snapshot with this all features allowed.
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.features.enable_all_deprecated_features", "true"}});

  // Now the same deprecation check should only trigger a warning.
  EXPECT_LOG_CONTAINS(
      "warning",
      "Using runtime overrides to continue using now fatal-by-default deprecated option "
      "'envoy.test.deprecation_test.Base.is_deprecated_fatal'",
      checkForDeprecation(base));
  EXPECT_EQ(1, runtime_deprecated_feature_use_.value());
}

TEST_P(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(DisallowViaRuntime)) {
  envoy::test::deprecation_test::Base base;
  base.set_is_deprecated("foo");

  EXPECT_LOG_CONTAINS("warning",
                      "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated'",
                      checkForDeprecation(base));
  EXPECT_EQ(1, runtime_deprecated_feature_use_.value());

  // Now create a new snapshot with this feature disallowed.
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.deprecated_features:envoy.test.deprecation_test.Base.is_deprecated", " false"}});

  EXPECT_THROW_WITH_REGEX(
      checkForDeprecation(base), Envoy::ProtobufMessage::DeprecatedProtoFieldException,
      "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated'");
  EXPECT_EQ(1, runtime_deprecated_feature_use_.value());

  // Verify that even when the enable_all_deprecated_features is enabled the
  // feature is disallowed.
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.features.enable_all_deprecated_features", "true"}});

  EXPECT_THROW_WITH_REGEX(
      checkForDeprecation(base), Envoy::ProtobufMessage::DeprecatedProtoFieldException,
      "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated'");
  EXPECT_EQ(1, runtime_deprecated_feature_use_.value());
}

// Note that given how Envoy config parsing works, the first time we hit a
// 'fatal' error and throw, we won't log future warnings. That said, this tests
// the case of the warning occurring before the fatal error.
TEST_P(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(MixOfFatalAndWarnings)) {
  envoy::test::deprecation_test::Base base;
  base.set_is_deprecated("foo");
  base.set_is_deprecated_fatal("foo");
  EXPECT_LOG_CONTAINS(
      "warning", "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated'", {
        EXPECT_THROW_WITH_REGEX(
            checkForDeprecation(base), Envoy::ProtobufMessage::DeprecatedProtoFieldException,
            "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated_fatal'");
      });
}

// Present (unused) deprecated messages should be detected as deprecated.
TEST_P(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(MessageDeprecated)) {
  envoy::test::deprecation_test::Base base;
  base.mutable_deprecated_message();
  EXPECT_LOG_CONTAINS(
      "warning", "Using deprecated option 'envoy.test.deprecation_test.Base.deprecated_message'",
      checkForDeprecation(base));
  EXPECT_EQ(1, runtime_deprecated_feature_use_.value());
}

TEST_P(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(InnerMessageDeprecated)) {
  envoy::test::deprecation_test::Base base;
  base.mutable_not_deprecated_message()->set_inner_not_deprecated("foo");
  // Checks for a non-deprecated field shouldn't trigger warnings
  EXPECT_LOG_NOT_CONTAINS("warning", "Using deprecated option", checkForDeprecation(base));

  base.mutable_not_deprecated_message()->set_inner_deprecated("bar");
  // Checks for a deprecated sub-message should result in a warning.
  EXPECT_LOG_CONTAINS(
      "warning",
      "Using deprecated option 'envoy.test.deprecation_test.Base.InnerMessage.inner_deprecated'",
      checkForDeprecation(base));
}

// Check that repeated sub-messages get validated.
TEST_P(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(SubMessageDeprecated)) {
  envoy::test::deprecation_test::Base base;
  base.add_repeated_message();
  base.add_repeated_message()->set_inner_deprecated("foo");
  base.add_repeated_message();

  // Fatal checks for a repeated deprecated sub-message should result in an exception.
  EXPECT_LOG_CONTAINS("warning",
                      "Using deprecated option "
                      "'envoy.test.deprecation_test.Base.InnerMessage.inner_deprecated'",
                      checkForDeprecation(base));
}

// Check that deprecated repeated messages trigger
TEST_P(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(RepeatedMessageDeprecated)) {
  envoy::test::deprecation_test::Base base;
  base.add_deprecated_repeated_message();

  // Fatal checks for a repeated deprecated sub-message should result in an exception.
  EXPECT_LOG_CONTAINS("warning",
                      "Using deprecated option "
                      "'envoy.test.deprecation_test.Base.deprecated_repeated_message'",
                      checkForDeprecation(base));
}

// Check that deprecated enum values trigger for default values
TEST_P(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(EnumValuesDeprecatedDefault)) {
  envoy::test::deprecation_test::Base base;
  base.mutable_enum_container();

  EXPECT_LOG_CONTAINS(
      "warning",
      "Using the default now-deprecated value DEPRECATED_DEFAULT for enum "
      "'envoy.test.deprecation_test.Base.InnerMessageWithDeprecationEnum.deprecated_enum' from "
      "file deprecated.proto. This enum value will be removed from Envoy soon so a non-default "
      "value must now be explicitly set.",
      checkForDeprecation(base));
}

// Check that deprecated enum values trigger for non-default values
TEST_P(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(EnumValuesDeprecated)) {
  envoy::test::deprecation_test::Base base;
  base.mutable_enum_container()->set_deprecated_enum(
      envoy::test::deprecation_test::Base::DEPRECATED_NOT_DEFAULT);

  EXPECT_LOG_CONTAINS(
      "warning",
      "Using deprecated value DEPRECATED_NOT_DEFAULT for enum "
      "'envoy.test.deprecation_test.Base.InnerMessageWithDeprecationEnum.deprecated_enum' "
      "from file deprecated.proto. This enum value will be removed from Envoy soon.",
      checkForDeprecation(base));
}

// Make sure the runtime overrides for protos work, by checking the non-fatal to
// fatal option.
TEST_P(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(RuntimeOverrideEnumDefault)) {
  envoy::test::deprecation_test::Base base;
  base.mutable_enum_container();

  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.deprecated_features:envoy.test.deprecation_test.Base.DEPRECATED_DEFAULT", "false"}});

  // Make sure this is set up right.
  EXPECT_THROW_WITH_REGEX(checkForDeprecation(base),
                          Envoy::ProtobufMessage::DeprecatedProtoFieldException,
                          "Using the default now-deprecated value DEPRECATED_DEFAULT");

  // Verify that even when the enable_all_deprecated_features is enabled the
  // enum is disallowed.
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.features.enable_all_deprecated_features", "true"}});

  EXPECT_THROW_WITH_REGEX(checkForDeprecation(base),
                          Envoy::ProtobufMessage::DeprecatedProtoFieldException,
                          "Using the default now-deprecated value DEPRECATED_DEFAULT");
}

// Make sure the runtime overrides for allowing fatal enums work.
TEST_P(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(FatalEnum)) {
  envoy::test::deprecation_test::Base base;
  base.mutable_enum_container()->set_deprecated_enum(
      envoy::test::deprecation_test::Base::DEPRECATED_FATAL);
  EXPECT_THROW_WITH_REGEX(checkForDeprecation(base),
                          Envoy::ProtobufMessage::DeprecatedProtoFieldException,
                          "Using deprecated value DEPRECATED_FATAL");

  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.deprecated_features:envoy.test.deprecation_test.Base.DEPRECATED_FATAL", "true"}});

  EXPECT_LOG_CONTAINS(
      "warning",
      "Using runtime overrides to continue using now fatal-by-default deprecated value "
      "DEPRECATED_FATAL for enum "
      "'envoy.test.deprecation_test.Base.InnerMessageWithDeprecationEnum.deprecated_enum' "
      "from file deprecated.proto. This enum value will be removed from Envoy soon.",
      checkForDeprecation(base));
}

// Make sure the runtime global override for allowing fatal enums work.
TEST_P(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(FatalEnumGlobalOverride)) {
  envoy::test::deprecation_test::Base base;
  base.mutable_enum_container()->set_deprecated_enum(
      envoy::test::deprecation_test::Base::DEPRECATED_FATAL);
  EXPECT_THROW_WITH_REGEX(checkForDeprecation(base),
                          Envoy::ProtobufMessage::DeprecatedProtoFieldException,
                          "Using deprecated value DEPRECATED_FATAL");

  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.features.enable_all_deprecated_features", "true"}});

  EXPECT_LOG_CONTAINS(
      "warning",
      "Using runtime overrides to continue using now fatal-by-default deprecated value "
      "DEPRECATED_FATAL for enum "
      "'envoy.test.deprecation_test.Base.InnerMessageWithDeprecationEnum.deprecated_enum' "
      "from file deprecated.proto. This enum value will be removed from Envoy soon.",
      checkForDeprecation(base));
}

// Verify that direct use of a hidden_envoy_deprecated field fails, but upgrade
// succeeds
TEST_P(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(ManualDeprecatedFieldAddition)) {
  // Create a base message and insert a deprecated field. When upgrading the
  // deprecated field should be set as deprecated, and a warning should be logged
  envoy::test::deprecation_test::Base base_should_warn =
      TestUtility::parseYaml<envoy::test::deprecation_test::Base>(R"EOF(
      not_deprecated: field1
      is_deprecated: hidden_field1
      not_deprecated_message:
        inner_not_deprecated: subfield1
      repeated_message:
        - inner_not_deprecated: subfield2
    )EOF");

  // Non-fatal checks for a deprecated field should log rather than throw an exception.
  EXPECT_LOG_CONTAINS("warning",
                      "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated'",
                      checkForDeprecation(base_should_warn));
  EXPECT_EQ(1, runtime_deprecated_feature_use_.value());
  EXPECT_EQ(1, deprecated_feature_seen_since_process_start_.value());

  // Create an upgraded message and insert a deprecated field. This is a bypass
  // of the upgrading procedure validation, and should fail
  envoy::test::deprecation_test::UpgradedBase base_should_fail =
      TestUtility::parseYaml<envoy::test::deprecation_test::UpgradedBase>(R"EOF(
      not_deprecated: field1
      hidden_envoy_deprecated_is_deprecated: hidden_field1
      not_deprecated_message:
        inner_not_deprecated: subfield1
      repeated_message:
        - inner_not_deprecated: subfield2
    )EOF");

  EXPECT_THROW_WITH_REGEX(
      MessageUtil::checkForUnexpectedFields(base_should_fail,
                                            ProtobufMessage::getStrictValidationVisitor()),
      ProtoValidationException,
      "Illegal use of hidden_envoy_deprecated_ V2 field "
      "'envoy.test.deprecation_test.UpgradedBase.hidden_envoy_deprecated_is_deprecated'");
  // The config will be rejected, so the feature will not be used.
  EXPECT_EQ(1, runtime_deprecated_feature_use_.value());
  EXPECT_EQ(1, deprecated_feature_seen_since_process_start_.value());
}

class TimestampUtilTest : public testing::Test, public ::testing::WithParamInterface<int64_t> {};

TEST_P(TimestampUtilTest, SystemClockToTimestampTest) {
  // Generate an input time_point<system_clock>,
  std::chrono::time_point<std::chrono::system_clock> epoch_time;
  auto time_original = epoch_time + std::chrono::milliseconds(GetParam());

  // And convert that to Timestamp.
  ProtobufWkt::Timestamp timestamp;
  TimestampUtil::systemClockToTimestamp(time_original, timestamp);

  // Then convert that Timestamp back into a time_point<system_clock>,
  std::chrono::time_point<std::chrono::system_clock> time_reflected =
      epoch_time +
      std::chrono::milliseconds(Protobuf::util::TimeUtil::TimestampToMilliseconds(timestamp));

  EXPECT_EQ(time_original, time_reflected);
}

INSTANTIATE_TEST_SUITE_P(TimestampUtilTestAcrossRange, TimestampUtilTest,
                         ::testing::Values(-1000 * 60 * 60 * 24 * 7, // week
                                           -1000 * 60 * 60 * 24,     // day
                                           -1000 * 60 * 60,          // hour
                                           -1000 * 60,               // minute
                                           -1000,                    // second
                                           -1,                       // millisecond
                                           0,
                                           1,                      // millisecond
                                           1000,                   // second
                                           1000 * 60,              // minute
                                           1000 * 60 * 60,         // hour
                                           1000 * 60 * 60 * 24,    // day
                                           1000 * 60 * 60 * 24 * 7 // week
                                           ));

TEST(StatusCode, Strings) {
  int last_code = static_cast<int>(ProtobufUtil::error::UNAUTHENTICATED);
  for (int i = 0; i < last_code; ++i) {
    EXPECT_NE(MessageUtil::CodeEnumToString(static_cast<ProtobufUtil::error::Code>(i)), "");
  }
  ASSERT_EQ("UNKNOWN",
            MessageUtil::CodeEnumToString(static_cast<ProtobufUtil::error::Code>(last_code + 1)));
  ASSERT_EQ("OK", MessageUtil::CodeEnumToString(ProtobufUtil::error::OK));
}

} // namespace Envoy
