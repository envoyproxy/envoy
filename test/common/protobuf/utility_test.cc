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
#include "envoy/config/health_checker/redis/v2/redis.pb.h"
#include "envoy/config/health_checker/redis/v2/redis.pb.validate.h"
#include "envoy/extensions/health_checkers/redis/v3/redis.pb.h"
#include "envoy/extensions/health_checkers/redis/v3/redis.pb.validate.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/base64.h"
#include "source/common/config/api_version.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_impl.h"

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

using testing::HasSubstr;

class RuntimeStatsHelper : public TestScopedRuntime {
public:
  RuntimeStatsHelper(bool allow_deprecated_v2_api = false)
      : runtime_deprecated_feature_use_(store_.counter("runtime.deprecated_feature_use")),
        deprecated_feature_seen_since_process_start_(
            store_.gauge("runtime.deprecated_feature_seen_since_process_start",
                         Stats::Gauge::ImportMode::NeverImport)) {
    if (allow_deprecated_v2_api) {
      Runtime::LoaderSingleton::getExisting()->mergeValues({
          {"envoy.test_only.broken_in_production.enable_deprecated_v2_api", "true"},
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
  auto status = MessageUtil::getJsonStringFromMessage(source_any, true).status();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.ToString(), testing::HasSubstr("bad.type.url"));
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

  // Test mixed case extension.
  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pB", bootstrap.SerializeAsString());

  envoy::config::bootstrap::v3::Bootstrap proto_from_file;
  TestUtility::loadFromFile(filename, proto_from_file, *api_);
  EXPECT_EQ(0, runtime_deprecated_feature_use_.value());
  EXPECT_TRUE(TestUtility::protoEqual(bootstrap, proto_from_file));
}

// Verify different YAML extensions using different cases.
TEST_F(ProtobufUtilityTest, YamlExtensions) {
  const std::string bootstrap_yaml = R"EOF(
layered_runtime:
  layers:
  - name: static_layer
    static_layer:
      foo: true)EOF";

  {
    const std::string filename =
        TestEnvironment::writeStringToFileForTest("proto.yAml", bootstrap_yaml);

    envoy::config::bootstrap::v3::Bootstrap proto_from_file;
    TestUtility::loadFromFile(filename, proto_from_file, *api_);
    TestUtility::validate(proto_from_file);
  }
  {
    const std::string filename =
        TestEnvironment::writeStringToFileForTest("proto.yMl", bootstrap_yaml);

    envoy::config::bootstrap::v3::Bootstrap proto_from_file;
    TestUtility::loadFromFile(filename, proto_from_file, *api_);
    TestUtility::validate(proto_from_file);
  }
}

// Verify different JSON extensions using different cases.
TEST_F(ProtobufUtilityTest, JsonExtensions) {
  const std::string bootstrap_json = R"EOF(
{
   "layered_runtime": {
      "layers": [
         {
            "name": "static_layer",
            "static_layer": {
               "foo": true
            }
         }
      ]
   }
})EOF";

  {
    const std::string filename =
        TestEnvironment::writeStringToFileForTest("proto.JSoN", bootstrap_json);

    envoy::config::bootstrap::v3::Bootstrap proto_from_file;
    TestUtility::loadFromFile(filename, proto_from_file, *api_);
    TestUtility::validate(proto_from_file);
  }
}

// An unknown field (or with wrong type) in a message is rejected.
TEST_F(ProtobufUtilityTest, LoadBinaryProtoUnknownFieldFromFile) {
  ProtobufWkt::Duration source_duration;
  source_duration.set_seconds(42);
  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb", source_duration.SerializeAsString());
  envoy::config::bootstrap::v3::Bootstrap proto_from_file;
  EXPECT_THROW_WITH_MESSAGE(TestUtility::loadFromFile(filename, proto_from_file, *api_),
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
  // Test mixed case extension.
  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pB_Text", bootstrap_text);

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

TEST_F(ProtobufUtilityTest, RedactTypedStructWithNoTypeUrl) {
  udpa::type::v1::TypedStruct actual;
  TestUtility::loadFromYaml(R"EOF(
value:
  sensitive_string: This field is sensitive, but we have no way of knowing.
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

TEST_F(ProtobufUtilityTest, RedactEmptyTypeUrlTypedStruct) {
  udpa::type::v1::TypedStruct actual;
  udpa::type::v1::TypedStruct expected = actual;
  MessageUtil::redact(actual);
  EXPECT_TRUE(TestUtility::protoEqual(expected, actual));
}

TEST_F(ProtobufUtilityTest, RedactEmptyTypeUrlAny) {
  ProtobufWkt::Any actual;
  MessageUtil::redact(actual);
  ProtobufWkt::Any expected = actual;
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

TEST_F(ProtobufUtilityTest, ValueUtilLoadFromYamlObjectWithIgnoredEntries) {
  EXPECT_EQ(ValueUtil::loadFromYaml("!ignore foo: bar\nbaz: qux").ShortDebugString(),
            "struct_value { fields { key: \"baz\" value { string_value: \"qux\" } } }");
}

TEST(LoadFromYamlExceptionTest, BadConversion) {
  std::string bad_yaml = R"EOF(
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /dev/null
  address:
    socket_address:
      address: {{ ntop_ip_loopback_address }}
      port_value: 0
)EOF";

  EXPECT_THROW_WITH_REGEX(ValueUtil::loadFromYaml(bad_yaml), EnvoyException, "bad conversion");
  EXPECT_THROW_WITHOUT_REGEX(ValueUtil::loadFromYaml(bad_yaml), EnvoyException,
                             "Unexpected YAML exception");
}

TEST(LoadFromYamlExceptionTest, ParserException) {
  std::string bad_yaml = R"EOF(
systemLog:
    destination: file
    path:"G:\file\path"
storage:
    dbPath:"G:\db\data"
)EOF";

  EXPECT_THROW_WITH_REGEX(ValueUtil::loadFromYaml(bad_yaml), EnvoyException, "illegal map value");
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

TEST_F(ProtobufUtilityTest, AnyBytes) {
  {
    ProtobufWkt::StringValue source;
    source.set_value("abc");
    ProtobufWkt::Any source_any;
    source_any.PackFrom(source);
    EXPECT_EQ(MessageUtil::anyToBytes(source_any), "abc");
  }
  {
    ProtobufWkt::BytesValue source;
    source.set_value("\x01\x02\x03");
    ProtobufWkt::Any source_any;
    source_any.PackFrom(source);
    EXPECT_EQ(MessageUtil::anyToBytes(source_any), "\x01\x02\x03");
  }
  {
    envoy::config::cluster::v3::Filter filter;
    ProtobufWkt::Any source_any;
    source_any.PackFrom(filter);
    EXPECT_EQ(MessageUtil::anyToBytes(source_any), source_any.value());
  }
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
                                ProtobufMessage::getStrictValidationVisitor()),
      EnvoyException, "INVALID_ARGUMENT:drain_connections_on_host_removal: Cannot find field.");
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
  int last_code = static_cast<int>(ProtobufUtil::StatusCode::kUnauthenticated);
  for (int i = 0; i < last_code; ++i) {
    EXPECT_NE(MessageUtil::codeEnumToString(static_cast<ProtobufUtil::StatusCode>(i)), "");
  }
  ASSERT_EQ("UNKNOWN",
            MessageUtil::codeEnumToString(static_cast<ProtobufUtil::StatusCode>(last_code + 1)));
  ASSERT_EQ("OK", MessageUtil::codeEnumToString(ProtobufUtil::StatusCode::kOk));
}

TEST(TypeUtilTest, TypeUrlHelperFunction) {
  EXPECT_EQ("envoy.config.filter.http.ip_tagging.v2.IPTagging",
            TypeUtil::typeUrlToDescriptorFullName(
                "type.googleapis.com/envoy.config.filter.http.ip_tagging.v2.IPTagging"));
  EXPECT_EQ(
      "type.googleapis.com/envoy.config.filter.http.ip_tagging.v2.IPTagging",
      TypeUtil::descriptorFullNameToTypeUrl("envoy.config.filter.http.ip_tagging.v2.IPTagging"));
}

class StructUtilTest : public ProtobufUtilityTest {
protected:
  ProtobufWkt::Struct updateSimpleStruct(const ProtobufWkt::Value& v0,
                                         const ProtobufWkt::Value& v1) {
    ProtobufWkt::Struct obj, with;
    (*obj.mutable_fields())["key"] = v0;
    (*with.mutable_fields())["key"] = v1;
    StructUtil::update(obj, with);
    EXPECT_EQ(obj.fields().size(), 1);
    return obj;
  }
};

TEST_F(StructUtilTest, StructUtilUpdateScalars) {
  {
    const auto obj = updateSimpleStruct(ValueUtil::stringValue("v0"), ValueUtil::stringValue("v1"));
    EXPECT_EQ(obj.fields().at("key").string_value(), "v1");
  }

  {
    const auto obj = updateSimpleStruct(ValueUtil::numberValue(0), ValueUtil::numberValue(1));
    EXPECT_EQ(obj.fields().at("key").number_value(), 1);
  }

  {
    const auto obj = updateSimpleStruct(ValueUtil::boolValue(false), ValueUtil::boolValue(true));
    EXPECT_EQ(obj.fields().at("key").bool_value(), true);
  }

  {
    const auto obj = updateSimpleStruct(ValueUtil::nullValue(), ValueUtil::nullValue());
    EXPECT_EQ(obj.fields().at("key").kind_case(), ProtobufWkt::Value::KindCase::kNullValue);
  }
}

TEST_F(StructUtilTest, StructUtilUpdateDifferentKind) {
  {
    const auto obj = updateSimpleStruct(ValueUtil::stringValue("v0"), ValueUtil::numberValue(1));
    auto& val = obj.fields().at("key");
    EXPECT_EQ(val.kind_case(), ProtobufWkt::Value::KindCase::kNumberValue);
    EXPECT_EQ(val.number_value(), 1);
  }

  {
    const auto obj =
        updateSimpleStruct(ValueUtil::structValue(MessageUtil::keyValueStruct("subkey", "v0")),
                           ValueUtil::stringValue("v1"));
    auto& val = obj.fields().at("key");
    EXPECT_EQ(val.kind_case(), ProtobufWkt::Value::KindCase::kStringValue);
    EXPECT_EQ(val.string_value(), "v1");
  }
}

TEST_F(StructUtilTest, StructUtilUpdateList) {
  ProtobufWkt::Struct obj, with;
  auto& list = *(*obj.mutable_fields())["key"].mutable_list_value();
  list.add_values()->set_string_value("v0");

  auto& with_list = *(*with.mutable_fields())["key"].mutable_list_value();
  with_list.add_values()->set_number_value(1);
  const auto v2 = MessageUtil::keyValueStruct("subkey", "str");
  *with_list.add_values()->mutable_struct_value() = v2;

  StructUtil::update(obj, with);
  ASSERT_THAT(obj.fields().size(), 1);
  const auto& list_vals = list.values();
  EXPECT_TRUE(ValueUtil::equal(list_vals[0], ValueUtil::stringValue("v0")));
  EXPECT_TRUE(ValueUtil::equal(list_vals[1], ValueUtil::numberValue(1)));
  EXPECT_TRUE(ValueUtil::equal(list_vals[2], ValueUtil::structValue(v2)));
}

TEST_F(StructUtilTest, StructUtilUpdateNewKey) {
  ProtobufWkt::Struct obj, with;
  (*obj.mutable_fields())["key0"].set_number_value(1);
  (*with.mutable_fields())["key1"].set_number_value(1);
  StructUtil::update(obj, with);

  const auto& fields = obj.fields();
  EXPECT_TRUE(ValueUtil::equal(fields.at("key0"), ValueUtil::numberValue(1)));
  EXPECT_TRUE(ValueUtil::equal(fields.at("key1"), ValueUtil::numberValue(1)));
}

TEST_F(StructUtilTest, StructUtilUpdateRecursiveStruct) {
  ProtobufWkt::Struct obj, with;
  *(*obj.mutable_fields())["tags"].mutable_struct_value() =
      MessageUtil::keyValueStruct("tag0", "1");
  *(*with.mutable_fields())["tags"].mutable_struct_value() =
      MessageUtil::keyValueStruct("tag1", "1");
  StructUtil::update(obj, with);

  ASSERT_EQ(obj.fields().at("tags").kind_case(), ProtobufWkt::Value::KindCase::kStructValue);
  const auto& tags = obj.fields().at("tags").struct_value().fields();
  EXPECT_TRUE(ValueUtil::equal(tags.at("tag0"), ValueUtil::stringValue("1")));
  EXPECT_TRUE(ValueUtil::equal(tags.at("tag1"), ValueUtil::stringValue("1")));
}

} // namespace Envoy
