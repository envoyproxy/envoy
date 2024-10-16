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

#include "source/common/common/base64.h"
#include "source/common/config/api_version.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_impl.h"

#include "test/common/protobuf/utility_test_file_wip.pb.h"
#include "test/common/protobuf/utility_test_file_wip_2.pb.h"
#include "test/common/protobuf/utility_test_message_field_wip.pb.h"
#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/proto/deprecated.pb.h"
#include "test/proto/sensitive.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/container/node_hash_set.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "udpa/type/v1/typed_struct.pb.h"
#include "xds/type/v3/typed_struct.pb.h"

using namespace std::chrono_literals;

namespace Envoy {

using ::testing::HasSubstr;

bool checkProtoEquality(const ProtobufWkt::Value& proto1, std::string text_proto2) {
  ProtobufWkt::Value proto2;
  if (!Protobuf::TextFormat::ParseFromString(text_proto2, &proto2)) {
    return false;
  }
  return Envoy::Protobuf::util::MessageDifferencer::Equals(proto1, proto2);
}

class RuntimeStatsHelper : public TestScopedRuntime {
public:
  explicit RuntimeStatsHelper()
      : runtime_deprecated_feature_use_(store_.counter("runtime.deprecated_feature_use")),
        deprecated_feature_seen_since_process_start_(
            store_.gauge("runtime.deprecated_feature_seen_since_process_start",
                         Stats::Gauge::ImportMode::NeverImport)) {

    auto visitor = static_cast<ProtobufMessage::StrictValidationVisitorImpl*>(
        &ProtobufMessage::getStrictValidationVisitor());
    visitor->setRuntime(loader());
  }
  ~RuntimeStatsHelper() {
    auto visitor = static_cast<ProtobufMessage::StrictValidationVisitorImpl*>(
        &ProtobufMessage::getStrictValidationVisitor());
    visitor->clearRuntime();
  }

  Stats::Counter& runtime_deprecated_feature_use_;
  Stats::Gauge& deprecated_feature_seen_since_process_start_;
};

class ProtobufUtilityTest : public testing::Test, protected RuntimeStatsHelper {};

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
  ProtobufWkt::Struct s2;
  (*s2.mutable_fields())["ab"].set_string_value("ij");
  (*s2.mutable_fields())["cde"].set_string_value("fgh");
  ProtobufWkt::Struct s3;
  (*s3.mutable_fields())["ac"].set_string_value("fgh");
  (*s3.mutable_fields())["cdb"].set_string_value("ij");

  ProtobufWkt::Any a1;
  a1.PackFrom(s);
  // The two base64 encoded Struct to test map is identical to the struct above, this tests whether
  // a map is deterministically serialized and hashed.
  ProtobufWkt::Any a2 = a1;
  a2.set_value(Base64::decode("CgsKA2NkZRIEGgJpagoLCgJhYhIFGgNmZ2g="));
  ProtobufWkt::Any a3 = a1;
  a3.set_value(Base64::decode("CgsKAmFiEgUaA2ZnaAoLCgNjZGUSBBoCaWo="));
  ProtobufWkt::Any a4, a5;
  a4.PackFrom(s2);
  a5.PackFrom(s3);

  EXPECT_EQ(MessageUtil::hash(a1), MessageUtil::hash(a2));
  EXPECT_EQ(MessageUtil::hash(a2), MessageUtil::hash(a3));
  EXPECT_NE(0, MessageUtil::hash(a1));
  // Same keys and values but with the values in a different order should not have
  // the same hash.
  EXPECT_NE(MessageUtil::hash(a1), MessageUtil::hash(a4));
  // Different keys with the values in the same order should not have the same hash.
  EXPECT_NE(MessageUtil::hash(a1), MessageUtil::hash(a5));
  // Struct without 'any' around it should not hash the same as struct inside 'any'.
  EXPECT_NE(MessageUtil::hash(s), MessageUtil::hash(a1));
}

TEST_F(ProtobufUtilityTest, RepeatedPtrUtilDebugString) {
  Protobuf::RepeatedPtrField<ProtobufWkt::UInt32Value> repeated;
  EXPECT_EQ("[]", RepeatedPtrUtil::debugString(repeated));
  repeated.Add()->set_value(10);
  EXPECT_THAT(RepeatedPtrUtil::debugString(repeated),
              testing::ContainsRegex("\\[.*[\n]*value:\\s*10\n\\]"));
  repeated.Add()->set_value(20);
  EXPECT_THAT(RepeatedPtrUtil::debugString(repeated),
              testing::ContainsRegex("\\[.*[\n]*value:\\s*10\n,.*[\n]*value:\\s*20\n\\]"));
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

namespace {
inline std::string unknownFieldsMessage(absl::string_view type_name,
                                        const std::vector<absl::string_view>& parent_paths,
                                        const std::vector<int>& field_numbers) {
  return fmt::format(
      "Protobuf message (type {}({}) with unknown field set {{{}}}) has unknown fields", type_name,
      !parent_paths.empty() ? absl::StrJoin(parent_paths, "::") : "root",
      absl::StrJoin(field_numbers, ", "));
}
} // namespace

// Validated exception thrown when downcastAndValidate observes a unknown field.
TEST_F(ProtobufUtilityTest, DowncastAndValidateUnknownFields) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  bootstrap.GetReflection()->MutableUnknownFields(&bootstrap)->AddVarint(1, 0);
  EXPECT_THROW_WITH_MESSAGE(TestUtility::validate(bootstrap), EnvoyException,
                            unknownFieldsMessage("envoy.config.bootstrap.v3.Bootstrap", {}, {1}));
  EXPECT_THROW_WITH_MESSAGE(TestUtility::validate(bootstrap), EnvoyException,
                            unknownFieldsMessage("envoy.config.bootstrap.v3.Bootstrap", {}, {1}));
}

// Validated exception thrown when downcastAndValidate observes a nested unknown field.
TEST_F(ProtobufUtilityTest, DowncastAndValidateUnknownFieldsNested) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
  cluster->GetReflection()->MutableUnknownFields(cluster)->AddVarint(1, 0);
  EXPECT_THROW_WITH_MESSAGE(TestUtility::validate(*cluster), EnvoyException,
                            unknownFieldsMessage("envoy.config.cluster.v3.Cluster", {}, {1}));
  EXPECT_THROW_WITH_MESSAGE(
      TestUtility::validate(bootstrap), EnvoyException,
      unknownFieldsMessage("envoy.config.cluster.v3.Cluster",
                           {"envoy.config.bootstrap.v3.Bootstrap",
                            "envoy.config.bootstrap.v3.Bootstrap.StaticResources"},
                           {1}));
}

// Validated exception thrown when observed nested unknown field with any.
TEST_F(ProtobufUtilityTest, ValidateUnknownFieldsNestedAny) {
  // Constructs a nested message with unknown field
  utility_test::message_field_wip::Outer outer;
  auto* inner = outer.mutable_inner();
  inner->set_name("inner");
  inner->GetReflection()->MutableUnknownFields(inner)->AddVarint(999, 0);

  // Constructs ancestors of the nested any message with unknown field.
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
  auto* cluster_type = cluster->mutable_cluster_type();
  cluster_type->set_name("outer");
  cluster_type->mutable_typed_config()->PackFrom(outer);

  EXPECT_THROW_WITH_MESSAGE(
      TestUtility::validate(bootstrap, /*recurse_into_any*/ true), EnvoyException,
      unknownFieldsMessage("utility_test.message_field_wip.Inner",
                           {
                               "envoy.config.bootstrap.v3.Bootstrap",
                               "envoy.config.bootstrap.v3.Bootstrap.StaticResources",
                               "envoy.config.cluster.v3.Cluster",
                               "envoy.config.cluster.v3.Cluster.CustomClusterType",
                               "google.protobuf.Any",
                               "utility_test.message_field_wip.Outer",
                           },
                           {999}));
}

TEST_F(ProtobufUtilityTest, JsonConvertAnyUnknownMessageType) {
  ProtobufWkt::Any source_any;
  source_any.set_type_url("type.googleapis.com/bad.type.url");
  source_any.set_value("asdf");
  auto status = MessageUtil::getJsonStringFromMessage(source_any, true).status();
  EXPECT_FALSE(status.ok());
}

TEST_F(ProtobufUtilityTest, JsonConvertKnownGoodMessage) {
  ProtobufWkt::Any source_any;
  source_any.PackFrom(envoy::config::bootstrap::v3::Bootstrap::default_instance());
  EXPECT_THAT(MessageUtil::getJsonStringFromMessageOrError(source_any, true),
              testing::HasSubstr("@type"));
}

TEST_F(ProtobufUtilityTest, JsonConvertOrErrorAnyWithUnknownMessageType) {
  ProtobufWkt::Any source_any;
  source_any.set_type_url("type.googleapis.com/bad.type.url");
  source_any.set_value("asdf");
  EXPECT_THAT(MessageUtil::getJsonStringFromMessageOrError(source_any),
              HasSubstr("Failed to convert"));
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

// Verify that a config with a deprecated field can be loaded with runtime global override.
TEST_F(ProtobufUtilityTest, DEPRECATED_FEATURE_TEST(LoadBinaryGlobalOverrideProtoFromFile)) {
  const std::string bootstrap_yaml = R"EOF(
layered_runtime:
  layers:
  - name: static_layer
    static_layer:
      envoy.features.enable_all_deprecated_features: true
watchdog: { miss_timeout: 1s })EOF";
  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.yaml", bootstrap_yaml);

  envoy::config::bootstrap::v3::Bootstrap proto_from_file;
  TestUtility::loadFromFile(filename, proto_from_file, *api_);
  TestUtility::validate(proto_from_file);
  EXPECT_TRUE(proto_from_file.has_watchdog());
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
                            unknownFieldsMessage("envoy.config.bootstrap.v3.Bootstrap", {}, {1}));
}

// Multiple unknown fields (or with wrong type) in a message are rejected.
TEST_F(ProtobufUtilityTest, LoadBinaryProtoUnknownMultipleFieldsFromFile) {
  ProtobufWkt::Duration source_duration;
  source_duration.set_seconds(42);
  source_duration.set_nanos(42);
  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb", source_duration.SerializeAsString());
  envoy::config::bootstrap::v3::Bootstrap proto_from_file;
  EXPECT_THROW_WITH_MESSAGE(
      TestUtility::loadFromFile(filename, proto_from_file, *api_), EnvoyException,
      unknownFieldsMessage("envoy.config.bootstrap.v3.Bootstrap", {}, {1, 2}));
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

// Fields that are values in a sensitive map should be redacted.
TEST_F(ProtobufUtilityTest, RedactMap) {
  envoy::test::Sensitive actual, expected;
  TestUtility::loadFromYaml(R"EOF(
sensitive_string_map:
  "a": "b"
sensitive_int_map:
  "x": 12345
insensitive_string_map:
  "c": "d"
insensitive_int_map:
  "y": 123456
)EOF",
                            actual);

  TestUtility::loadFromYaml(R"EOF(
sensitive_string_map:
  "a": "[redacted]"
sensitive_int_map:
  "x":
insensitive_string_map:
  "c": "d"
insensitive_int_map:
  "y": 123456
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

template <typename T> class TypedStructUtilityTest : public ProtobufUtilityTest {};

using TypedStructTypes = ::testing::Types<xds::type::v3::TypedStruct, udpa::type::v1::TypedStruct>;
TYPED_TEST_SUITE(TypedStructUtilityTest, TypedStructTypes);

// Empty `TypedStruct` can be trivially redacted.
TYPED_TEST(TypedStructUtilityTest, RedactEmptyTypedStruct) {
  TypeParam actual;
  TestUtility::loadFromYaml(R"EOF(
type_url: type.googleapis.com/envoy.test.Sensitive
)EOF",
                            actual);

  TypeParam expected = actual;
  MessageUtil::redact(actual);
  EXPECT_TRUE(TestUtility::protoEqual(expected, actual));
}

TYPED_TEST(TypedStructUtilityTest, RedactTypedStructWithNoTypeUrl) {
  TypeParam actual;
  TestUtility::loadFromYaml(R"EOF(
value:
  sensitive_string: This field is sensitive, but we have no way of knowing.
)EOF",
                            actual);

  TypeParam expected = actual;
  MessageUtil::redact(actual);
  EXPECT_TRUE(TestUtility::protoEqual(expected, actual));
}

// Messages packed into `TypedStruct` with unknown type URLs are skipped.
TYPED_TEST(TypedStructUtilityTest, RedactTypedStructWithUnknownTypeUrl) {
  TypeParam actual;
  TestUtility::loadFromYaml(R"EOF(
type_url: type.googleapis.com/envoy.unknown.Message
value:
  sensitive_string: This field is sensitive, but we have no way of knowing.
)EOF",
                            actual);

  TypeParam expected = actual;
  MessageUtil::redact(actual);
  EXPECT_TRUE(TestUtility::protoEqual(expected, actual));
}

TYPED_TEST(TypedStructUtilityTest, RedactTypedStructWithErrorContent) {
  envoy::test::Sensitive actual;
  TestUtility::loadFromYaml(R"EOF(
insensitive_typed_struct:
  type_url: type.googleapis.com/envoy.test.Sensitive
  value:
    # The target field is string but value here is int.
    insensitive_string: 123
    # The target field is int but value here is string.
    insensitive_int: "abc"
)EOF",
                            actual);

  EXPECT_NO_THROW(MessageUtil::redact(actual));
}

TYPED_TEST(TypedStructUtilityTest, RedactEmptyTypeUrlTypedStruct) {
  TypeParam actual;
  TypeParam expected = actual;
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

TEST_F(ProtobufUtilityTest, SanitizeUTF8) {
  {
    absl::string_view original("already valid");
    std::string sanitized = MessageUtil::sanitizeUtf8String(original);

    EXPECT_EQ(sanitized, original);
  }

  {
    // Create a string that isn't valid UTF-8, that contains multiple sections of
    // invalid characters.
    std::string original("valid_prefix");
    original.append(1, char(0xc3));
    original.append(1, char(0xc7));
    original.append("valid_middle");
    original.append(1, char(0xc4));
    original.append("valid_suffix");

    std::string sanitized = MessageUtil::sanitizeUtf8String(original);
    EXPECT_EQ(absl::string_view("valid_prefix!!valid_middle!valid_suffix"), sanitized);
    EXPECT_EQ(sanitized.length(), original.length());
  }
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
  EXPECT_TRUE(checkProtoEquality(ValueUtil::loadFromYaml("null"), "null_value: NULL_VALUE"));
  EXPECT_TRUE(checkProtoEquality(ValueUtil::loadFromYaml("true"), "bool_value: true"));
  EXPECT_TRUE(checkProtoEquality(ValueUtil::loadFromYaml("1"), "number_value: 1"));
  EXPECT_TRUE(checkProtoEquality(ValueUtil::loadFromYaml("9223372036854775807"),
                                 "string_value: \"9223372036854775807\""));
  EXPECT_TRUE(checkProtoEquality(ValueUtil::loadFromYaml("\"foo\""), "string_value: \"foo\""));
  EXPECT_TRUE(checkProtoEquality(ValueUtil::loadFromYaml("foo"), "string_value: \"foo\""));
}

TEST_F(ProtobufUtilityTest, ValueUtilLoadFromYamlObject) {
  EXPECT_TRUE(checkProtoEquality(
      ValueUtil::loadFromYaml("[foo, bar]"),
      "list_value { values { string_value: \"foo\" } values { string_value: \"bar\" } }"));
  EXPECT_TRUE(checkProtoEquality(
      ValueUtil::loadFromYaml("foo: bar"),
      "struct_value { fields { key: \"foo\" value { string_value: \"bar\" } } }"));
}

TEST_F(ProtobufUtilityTest, ValueUtilLoadFromYamlObjectWithIgnoredEntries) {
  EXPECT_TRUE(checkProtoEquality(
      ValueUtil::loadFromYaml("!ignore foo: bar\nbaz: qux"),
      "struct_value { fields { key: \"baz\" value { string_value: \"qux\" } } }"));
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
      R"(Unable to unpack as google.protobuf.Timestamp:.*[\n]*\[type.googleapis.com/google.protobuf.Duration\] .*)");
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

// MessageUtility::unpackToOrThrow() with the wrong type throws.
TEST_F(ProtobufUtilityTest, UnpackToWrongType) {
  ProtobufWkt::Duration source_duration;
  source_duration.set_seconds(42);
  ProtobufWkt::Any source_any;
  source_any.PackFrom(source_duration);
  ProtobufWkt::Timestamp dst;
  EXPECT_THROW_WITH_REGEX(
      MessageUtil::unpackToOrThrow(source_any, dst), EnvoyException,
      R"(Unable to unpack as google.protobuf.Timestamp:.*[\n]*\[type.googleapis.com/google.protobuf.Duration\] .*)");
}

// MessageUtility::unpackToOrThrow() with API message works at same version.
TEST_F(ProtobufUtilityTest, UnpackToSameVersion) {
  {
    API_NO_BOOST(envoy::api::v2::Cluster) source;
    source.set_drain_connections_on_host_removal(true);
    ProtobufWkt::Any source_any;
    source_any.PackFrom(source);
    API_NO_BOOST(envoy::api::v2::Cluster) dst;
    MessageUtil::unpackToOrThrow(source_any, dst);
    EXPECT_TRUE(dst.drain_connections_on_host_removal());
  }
  {
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) source;
    source.set_ignore_health_on_host_removal(true);
    ProtobufWkt::Any source_any;
    source_any.PackFrom(source);
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) dst;
    MessageUtil::unpackToOrThrow(source_any, dst);
    EXPECT_TRUE(dst.ignore_health_on_host_removal());
  }
}

// MessageUtility::unpackTo() with the right type.
TEST_F(ProtobufUtilityTest, UnpackToNoThrowRightType) {
  ProtobufWkt::Duration src_duration;
  src_duration.set_seconds(42);
  ProtobufWkt::Any source_any;
  source_any.PackFrom(src_duration);
  ProtobufWkt::Duration dst_duration;
  EXPECT_OK(MessageUtil::unpackTo(source_any, dst_duration));
  // Source and destination are expected to be equal.
  EXPECT_EQ(src_duration, dst_duration);
}

// MessageUtility::unpackTo() with the wrong type.
TEST_F(ProtobufUtilityTest, UnpackToNoThrowWrongType) {
  ProtobufWkt::Duration source_duration;
  source_duration.set_seconds(42);
  ProtobufWkt::Any source_any;
  source_any.PackFrom(source_duration);
  ProtobufWkt::Timestamp dst;
  auto status = MessageUtil::unpackTo(source_any, dst);
  EXPECT_TRUE(absl::IsInternal(status));
  EXPECT_THAT(
      std::string(status.message()),
      testing::ContainsRegex("Unable to unpack as google.protobuf.Timestamp: "
                             ".*[\n]*\\[type.googleapis.com/google.protobuf.Duration\\] .*"));
}

// MessageUtility::loadFromJson() throws on garbage JSON.
TEST_F(ProtobufUtilityTest, LoadFromJsonGarbage) {
  envoy::config::cluster::v3::Cluster dst;
  EXPECT_THROW(MessageUtil::loadFromJson("{drain_connections_on_host_removal: true", dst,
                                         ProtobufMessage::getNullValidationVisitor()),
               EnvoyException);
}

// MessageUtility::loadFromJson() with API message works at same version.
TEST_F(ProtobufUtilityTest, LoadFromJsonSameVersion) {
  {
    API_NO_BOOST(envoy::api::v2::Cluster) dst;
    MessageUtil::loadFromJson("{drain_connections_on_host_removal: true}", dst,
                              ProtobufMessage::getNullValidationVisitor());
    EXPECT_TRUE(dst.drain_connections_on_host_removal());
  }
  {
    API_NO_BOOST(envoy::api::v2::Cluster) dst;
    MessageUtil::loadFromJson("{drain_connections_on_host_removal: true}", dst,
                              ProtobufMessage::getStrictValidationVisitor());
    EXPECT_TRUE(dst.drain_connections_on_host_removal());
  }
  {
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) dst;
    MessageUtil::loadFromJson("{ignore_health_on_host_removal: true}", dst,
                              ProtobufMessage::getNullValidationVisitor());
    EXPECT_TRUE(dst.ignore_health_on_host_removal());
  }
  {
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) dst;
    MessageUtil::loadFromJson("{ignore_health_on_host_removal: true}", dst,
                              ProtobufMessage::getStrictValidationVisitor());
    EXPECT_TRUE(dst.ignore_health_on_host_removal());
  }
}

// MessageUtility::loadFromJson() avoids boosting when version specified.
TEST_F(ProtobufUtilityTest, LoadFromJsonNoBoosting) {
  envoy::config::cluster::v3::Cluster dst;
  EXPECT_THROW(MessageUtil::loadFromJson("{drain_connections_on_host_removal: true}", dst,
                                         ProtobufMessage::getStrictValidationVisitor()),
               EnvoyException);
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
  std::string expected_duration_text = R"pb(seconds: -281474976710656)pb";
  ProtobufWkt::Duration expected_duration_proto;
  Protobuf::TextFormat::ParseFromString(expected_duration_text, &expected_duration_proto);
  EXPECT_THROW(TestUtility::jsonConvert(source_duration, dest_struct), EnvoyException);
}

// Regression test for https://github.com/envoyproxy/envoy/issues/3665.
TEST_F(ProtobufUtilityTest, JsonConvertCamelSnake) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  // Make sure we use a field eligible for snake/camel case translation.
  bootstrap.mutable_cluster_manager()->set_local_cluster_name("foo");
  ProtobufWkt::Struct json;
  TestUtility::jsonConvert(bootstrap, json);
  // Verify we can round-trip. This didn't cause the #3665 regression, but useful as a sanity check.
  TestUtility::loadFromJson(MessageUtil::getJsonStringFromMessageOrError(json, false), bootstrap);
  // Verify we don't do a camel case conversion.
  EXPECT_EQ("foo", json.fields()
                       .at("cluster_manager")
                       .struct_value()
                       .fields()
                       .at("local_cluster_name")
                       .string_value());
}

// Test the jsonConvertValue in both success and failure modes.
TEST_F(ProtobufUtilityTest, JsonConvertValueSuccess) {
  {
    envoy::config::bootstrap::v3::Bootstrap source;
    source.set_flags_path("foo");
    ProtobufWkt::Value tmp;
    envoy::config::bootstrap::v3::Bootstrap dest;
    EXPECT_TRUE(MessageUtil::jsonConvertValue(source, tmp));
    TestUtility::jsonConvert(tmp, dest);
    EXPECT_EQ("foo", dest.flags_path());
  }

  {
    ProtobufWkt::StringValue source;
    source.set_value("foo");
    ProtobufWkt::Value dest;
    EXPECT_TRUE(MessageUtil::jsonConvertValue(source, dest));

    ProtobufWkt::Value expected;
    expected.set_string_value("foo");
    EXPECT_THAT(dest, ProtoEq(expected));
  }

  {
    ProtobufWkt::Duration source;
    source.set_seconds(-281474976710656);
    ProtobufWkt::Value dest;
    EXPECT_FALSE(MessageUtil::jsonConvertValue(source, dest));
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
  // Verify loadFromYaml throws error when the input leads to an Array.
  EXPECT_THROW(TestUtility::loadFromYaml("- node: { id: node1 }", bootstrap), EnvoyException);
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
  // Once the runtime feature "envoy.reloadable_features.strict_duration_validation"
  // is deprecated, this test should only validate the "true" case.
  for (const std::string strict_duration : {"true", "false"}) {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues(
        {{"envoy.reloadable_features.strict_duration_validation", strict_duration}});
    {
      ProtobufWkt::Duration duration;
      duration.set_seconds(-1);
      EXPECT_THROW(DurationUtil::durationToMilliseconds(duration), EnvoyException);
    }
    {
      ProtobufWkt::Duration duration;
      duration.set_nanos(-1);
      EXPECT_THROW(DurationUtil::durationToMilliseconds(duration), EnvoyException);
    }
    // Invalid number of nanoseconds.
    {
      ProtobufWkt::Duration duration;
      duration.set_nanos(1000000000);
      EXPECT_THROW(DurationUtil::durationToMilliseconds(duration), EnvoyException);
    }
    {
      ProtobufWkt::Duration duration;
      duration.set_seconds(Protobuf::util::TimeUtil::kDurationMaxSeconds + 1);
      EXPECT_THROW(DurationUtil::durationToMilliseconds(duration), EnvoyException);
    }
    // Invalid number of seconds.
    {
      ProtobufWkt::Duration duration;
      constexpr int64_t kMaxInt64Nanoseconds =
          (std::numeric_limits<int64_t>::max() - 999999999) / (1000 * 1000 * 1000);
      duration.set_seconds(kMaxInt64Nanoseconds + 1);
      // Once the runtime feature "envoy.reloadable_features.strict_duration_validation"
      // is deprecated, this test should only validate EXPECT_THROW.
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.strict_duration_validation")) {
        EXPECT_THROW(DurationUtil::durationToMilliseconds(duration), EnvoyException);
      } else {
        EXPECT_NO_THROW(DurationUtil::durationToMilliseconds(duration));
      }
    }
    // Max valid seconds and nanoseconds.
    {
      ProtobufWkt::Duration duration;
      constexpr int64_t kMaxInt64Nanoseconds =
          (std::numeric_limits<int64_t>::max() - 999999999) / (1000 * 1000 * 1000);
      duration.set_seconds(kMaxInt64Nanoseconds);
      duration.set_nanos(999999999);
      EXPECT_NO_THROW(DurationUtil::durationToMilliseconds(duration));
    }
    // Invalid combined seconds and nanoseconds.
    {
      // Once the runtime feature "envoy.reloadable_features.strict_duration_validation"
      // is deprecated, this test should be executed unconditionally. The test is only
      // with the flag because without the flag set to false it will trigger a
      // runtime error (crash) with the current ASAN test suite.
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.strict_duration_validation")) {
        ProtobufWkt::Duration duration;
        constexpr int64_t kMaxInt64Nanoseconds =
            std::numeric_limits<int64_t>::max() / (1000 * 1000 * 1000);
        duration.set_seconds(kMaxInt64Nanoseconds);
        duration.set_nanos(999999999);
        EXPECT_THROW(DurationUtil::durationToMilliseconds(duration), EnvoyException);
      }
    }
  }
}

TEST(DurationUtilTest, NoThrow) {
  // Once the runtime feature "envoy.reloadable_features.strict_duration_validation"
  // is deprecated, this test should only validate the "true" case.
  for (const std::string strict_duration : {"true", "false"}) {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues(
        {{"envoy.reloadable_features.strict_duration_validation", strict_duration}});
    {
      // In range test
      ProtobufWkt::Duration duration;
      duration.set_seconds(5);
      duration.set_nanos(10000000);
      const auto result = DurationUtil::durationToMillisecondsNoThrow(duration);
      EXPECT_TRUE(result.ok());
      EXPECT_TRUE(result.value() == 5010);
    }
    // Below are out-of-range tests
    {
      ProtobufWkt::Duration duration;
      duration.set_seconds(-1);
      const auto result = DurationUtil::durationToMillisecondsNoThrow(duration);
      EXPECT_FALSE(result.ok());
    }
    {
      ProtobufWkt::Duration duration;
      duration.set_nanos(-1);
      const auto result = DurationUtil::durationToMillisecondsNoThrow(duration);
      EXPECT_FALSE(result.ok());
    }
    // Invalid number of nanoseconds.
    {
      ProtobufWkt::Duration duration;
      duration.set_nanos(1000000000);
      const auto result = DurationUtil::durationToMillisecondsNoThrow(duration);
      EXPECT_FALSE(result.ok());
    }
    {
      ProtobufWkt::Duration duration;
      duration.set_seconds(Protobuf::util::TimeUtil::kDurationMaxSeconds + 1);
      const auto result = DurationUtil::durationToMillisecondsNoThrow(duration);
      EXPECT_FALSE(result.ok());
    }
    // Invalid number of seconds.
    {
      ProtobufWkt::Duration duration;
      constexpr int64_t kMaxInt64Nanoseconds =
          (std::numeric_limits<int64_t>::max() - 999999999) / (1000 * 1000 * 1000);
      duration.set_seconds(kMaxInt64Nanoseconds + 1);
      const auto result = DurationUtil::durationToMillisecondsNoThrow(duration);
      // Once the runtime feature "envoy.reloadable_features.strict_duration_validation"
      // is deprecated, this test should only validate EXPECT_FALSE.
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.strict_duration_validation")) {
        EXPECT_FALSE(result.ok());
      } else {
        EXPECT_TRUE(result.ok());
      }
    }
    // Max valid seconds and nanoseconds.
    {
      ProtobufWkt::Duration duration;
      constexpr int64_t kMaxInt64Nanoseconds =
          (std::numeric_limits<int64_t>::max() - 999999999) / (1000 * 1000 * 1000);
      duration.set_seconds(kMaxInt64Nanoseconds);
      duration.set_nanos(999999999);
      const auto result = DurationUtil::durationToMillisecondsNoThrow(duration);
      EXPECT_TRUE(result.ok());
    }
    // Invalid combined seconds and nanoseconds.
    {
      // Once the runtime feature "envoy.reloadable_features.strict_duration_validation"
      // is deprecated, this test should be executed unconditionally. The test is only
      // with the flag because without the flag set to false it will trigger a
      // runtime error (crash) with the current ASAN test suite.
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.strict_duration_validation")) {
        ProtobufWkt::Duration duration;
        constexpr int64_t kMaxInt64Nanoseconds =
            std::numeric_limits<int64_t>::max() / (1000 * 1000 * 1000);
        duration.set_seconds(kMaxInt64Nanoseconds);
        duration.set_nanos(999999999);
        const auto result = DurationUtil::durationToMillisecondsNoThrow(duration);
        EXPECT_FALSE(result.ok());
      }
    }
  }
}

// Validate that the duration in a message is validated correctly.
TEST_F(ProtobufUtilityTest, MessageDurationValidation) {
  // Once the runtime key is deprecated, the scoped_runtime should be removed.
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.validate_duration_in_configs", "true"}});

  {
    envoy::config::bootstrap::v3::Bootstrap bootstrap;
    bootstrap.mutable_stats_flush_interval()->set_seconds(1);
    EXPECT_NO_THROW(MessageUtil::validateDurationFields(bootstrap));
  }
  // Invalid durations.
  {
    envoy::config::bootstrap::v3::Bootstrap bootstrap;
    bootstrap.mutable_stats_flush_interval()->set_seconds(-1);
    EXPECT_THROW_WITH_REGEX(MessageUtil::validateDurationFields(bootstrap), EnvoyException,
                            "Invalid duration: Expected positive duration");
  }
  {
    envoy::config::bootstrap::v3::Bootstrap bootstrap;
    bootstrap.mutable_stats_flush_interval()->set_seconds(1);
    bootstrap.mutable_stats_flush_interval()->set_nanos(-100);
    EXPECT_THROW_WITH_REGEX(MessageUtil::validateDurationFields(bootstrap), EnvoyException,
                            "Invalid duration: Expected positive duration");
  }
}

// Verify WIP accounting of the file based annotations. This test uses the strict validator to test
// that code path.
TEST_F(ProtobufUtilityTest, MessageInWipFile) {
  Stats::TestUtil::TestStore stats;
  Stats::Counter& wip_counter = stats.counter("wip_counter");
  ProtobufMessage::StrictValidationVisitorImpl validation_visitor;

  utility_test::file_wip::Foo foo;
  EXPECT_LOG_CONTAINS(
      "warning",
      "message 'utility_test.file_wip.Foo' is contained in proto file "
      "'test/common/protobuf/utility_test_file_wip.proto' marked as work-in-progress. API features "
      "marked as work-in-progress are not considered stable, are not covered by the threat model, "
      "are not supported by the security team, and are subject to breaking changes. Do not use "
      "this feature without understanding each of the previous points.",
      MessageUtil::checkForUnexpectedFields(foo, validation_visitor));

  EXPECT_EQ(0, wip_counter.value());
  validation_visitor.setCounters(wip_counter);
  EXPECT_EQ(1, wip_counter.value());

  utility_test::file_wip_2::Foo foo2;
  EXPECT_LOG_CONTAINS(
      "warning",
      "message 'utility_test.file_wip_2.Foo' is contained in proto file "
      "'test/common/protobuf/utility_test_file_wip_2.proto' marked as work-in-progress. API "
      "features marked as work-in-progress are not considered stable, are not covered by the "
      "threat model, are not supported by the security team, and are subject to breaking changes. "
      "Do not use this feature without understanding each of the previous points.",
      MessageUtil::checkForUnexpectedFields(foo2, validation_visitor));

  EXPECT_EQ(2, wip_counter.value());
}

// Verify WIP accounting for message and field annotations. This test uses the warning validator
// to test that code path.
TEST_F(ProtobufUtilityTest, MessageWip) {
  Stats::TestUtil::TestStore stats;
  Stats::Counter& unknown_counter = stats.counter("unknown_counter");
  Stats::Counter& wip_counter = stats.counter("wip_counter");
  ProtobufMessage::WarningValidationVisitorImpl validation_visitor;

  utility_test::message_field_wip::Foo foo;
  EXPECT_LOG_CONTAINS(
      "warning",
      "message 'utility_test.message_field_wip.Foo' is marked as work-in-progress. API features "
      "marked as work-in-progress are not considered stable, are not covered by the threat model, "
      "are not supported by the security team, and are subject to breaking changes. Do not use "
      "this feature without understanding each of the previous points.",
      MessageUtil::checkForUnexpectedFields(foo, validation_visitor));

  EXPECT_EQ(0, wip_counter.value());
  validation_visitor.setCounters(unknown_counter, wip_counter);
  EXPECT_EQ(1, wip_counter.value());

  utility_test::message_field_wip::Bar bar;
  EXPECT_NO_LOGS(MessageUtil::checkForUnexpectedFields(bar, validation_visitor));

  bar.set_test_field(true);
  EXPECT_LOG_CONTAINS(
      "warning",
      "field 'utility_test.message_field_wip.Bar.test_field' is marked as work-in-progress. API "
      "features marked as work-in-progress are not considered stable, are not covered by the "
      "threat model, are not supported by the security team, and are subject to breaking changes. "
      "Do not use this feature without understanding each of the previous points.",
      MessageUtil::checkForUnexpectedFields(bar, validation_visitor));

  EXPECT_EQ(2, wip_counter.value());
}

class DeprecatedFieldsTest : public testing::Test, protected RuntimeStatsHelper {
protected:
  void checkForDeprecation(const Protobuf::Message& message) {
    MessageUtil::checkForUnexpectedFields(message, ProtobufMessage::getStrictValidationVisitor());
  }
};

TEST_F(DeprecatedFieldsTest, NoCrashIfRuntimeMissing) {
  runtime_.reset();

  envoy::test::deprecation_test::Base base;
  base.set_not_deprecated("foo");
  // Fatal checks for a non-deprecated field should cause no problem.
  checkForDeprecation(base);
}

TEST_F(DeprecatedFieldsTest, NoErrorWhenDeprecatedFieldsUnused) {
  envoy::test::deprecation_test::Base base;
  base.set_not_deprecated("foo");
  // Fatal checks for a non-deprecated field should cause no problem.
  checkForDeprecation(base);
  EXPECT_EQ(0, runtime_deprecated_feature_use_.value());
  EXPECT_EQ(0, deprecated_feature_seen_since_process_start_.value());
}

TEST_F(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(IndividualFieldDeprecatedEmitsError)) {
  envoy::test::deprecation_test::Base base;
  base.set_is_deprecated("foo");
  // Non-fatal checks for a deprecated field should log rather than throw an exception.
  EXPECT_LOG_CONTAINS("warning",
                      "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated'",
                      checkForDeprecation(base));
  EXPECT_EQ(1, runtime_deprecated_feature_use_.value());
  EXPECT_EQ(1, deprecated_feature_seen_since_process_start_.value());
}

TEST_F(DeprecatedFieldsTest, IndividualFieldDeprecatedEmitsCrash) {
  envoy::test::deprecation_test::Base base;
  base.set_is_deprecated("foo");
  // Non-fatal checks for a deprecated field should throw an exception if the
  // runtime flag is enabled..
  mergeValues({
      {"envoy.features.fail_on_any_deprecated_feature", "true"},
  });
  EXPECT_THROW_WITH_REGEX(
      checkForDeprecation(base), Envoy::EnvoyException,
      "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated'");
  EXPECT_EQ(0, runtime_deprecated_feature_use_.value());
  EXPECT_EQ(0, deprecated_feature_seen_since_process_start_.value());
}

// Use of a deprecated and disallowed field should result in an exception.
TEST_F(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(IndividualFieldDisallowed)) {
  envoy::test::deprecation_test::Base base;
  base.set_is_deprecated_fatal("foo");
  EXPECT_THROW_WITH_REGEX(
      checkForDeprecation(base), Envoy::EnvoyException,
      "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated_fatal'");
}

TEST_F(DeprecatedFieldsTest,
       DEPRECATED_FEATURE_TEST(IndividualFieldDisallowedWithRuntimeOverride)) {
  envoy::test::deprecation_test::Base base;
  base.set_is_deprecated_fatal("foo");

  // Make sure this is set up right.
  EXPECT_THROW_WITH_REGEX(
      checkForDeprecation(base), Envoy::EnvoyException,
      "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated_fatal'");
  // The config will be rejected, so the feature will not be used.

  // Now create a new snapshot with this feature allowed.
  mergeValues({{"envoy.deprecated_features:envoy.test.deprecation_test.Base.is_deprecated_fatal",
                "True "}});

  // Now the same deprecation check should only trigger a warning.
  EXPECT_LOG_CONTAINS(
      "warning",
      "Using runtime overrides to continue using now fatal-by-default deprecated option "
      "'envoy.test.deprecation_test.Base.is_deprecated_fatal'",
      checkForDeprecation(base));
}

// Test that a deprecated field is allowed with runtime global override.
TEST_F(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(IndividualFieldDisallowedWithGlobalOverride)) {
  envoy::test::deprecation_test::Base base;
  base.set_is_deprecated_fatal("foo");

  // Make sure this is set up right.
  EXPECT_THROW_WITH_REGEX(
      checkForDeprecation(base), Envoy::EnvoyException,
      "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated_fatal'");
  // The config will be rejected, so the feature will not be used.

  // Now create a new snapshot with this all features allowed.
  mergeValues({{"envoy.features.enable_all_deprecated_features", "true"}});

  // Now the same deprecation check should only trigger a warning.
  EXPECT_LOG_CONTAINS(
      "warning",
      "Using runtime overrides to continue using now fatal-by-default deprecated option "
      "'envoy.test.deprecation_test.Base.is_deprecated_fatal'",
      checkForDeprecation(base));
}

TEST_F(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(DisallowViaRuntime)) {
  envoy::test::deprecation_test::Base base;
  base.set_is_deprecated("foo");

  EXPECT_LOG_CONTAINS("warning",
                      "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated'",
                      checkForDeprecation(base));

  // Now create a new snapshot with this feature disallowed.
  mergeValues(
      {{"envoy.deprecated_features:envoy.test.deprecation_test.Base.is_deprecated", " false"}});

  EXPECT_THROW_WITH_REGEX(
      checkForDeprecation(base), Envoy::EnvoyException,
      "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated'");

  // Verify that even when the enable_all_deprecated_features is enabled the
  // feature is disallowed.
  mergeValues({{"envoy.features.enable_all_deprecated_features", "true"}});

  EXPECT_THROW_WITH_REGEX(
      checkForDeprecation(base), Envoy::EnvoyException,
      "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated'");
}

// Note that given how Envoy config parsing works, the first time we hit a
// 'fatal' error and throw, we won't log future warnings. That said, this tests
// the case of the warning occurring before the fatal error.
TEST_F(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(MixOfFatalAndWarnings)) {
  envoy::test::deprecation_test::Base base;
  base.set_is_deprecated("foo");
  base.set_is_deprecated_fatal("foo");
  EXPECT_LOG_CONTAINS(
      "warning", "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated'", {
        EXPECT_THROW_WITH_REGEX(
            checkForDeprecation(base), Envoy::EnvoyException,
            "Using deprecated option 'envoy.test.deprecation_test.Base.is_deprecated_fatal'");
      });
}

// Present (unused) deprecated messages should be detected as deprecated.
TEST_F(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(MessageDeprecated)) {
  envoy::test::deprecation_test::Base base;
  base.mutable_deprecated_message();
  EXPECT_LOG_CONTAINS(
      "warning", "Using deprecated option 'envoy.test.deprecation_test.Base.deprecated_message'",
      checkForDeprecation(base));
}

TEST_F(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(InnerMessageDeprecated)) {
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
TEST_F(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(SubMessageDeprecated)) {
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
TEST_F(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(RepeatedMessageDeprecated)) {
  envoy::test::deprecation_test::Base base;
  base.add_deprecated_repeated_message();

  // Fatal checks for a repeated deprecated sub-message should result in an exception.
  EXPECT_LOG_CONTAINS("warning",
                      "Using deprecated option "
                      "'envoy.test.deprecation_test.Base.deprecated_repeated_message'",
                      checkForDeprecation(base));
}

// Check that deprecated enum values trigger for default values
TEST_F(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(EnumValuesDeprecatedDefault)) {
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
TEST_F(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(EnumValuesDeprecated)) {
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
TEST_F(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(RuntimeOverrideEnumDefault)) {
  envoy::test::deprecation_test::Base base;
  base.mutable_enum_container();

  mergeValues(
      {{"envoy.deprecated_features:envoy.test.deprecation_test.Base.DEPRECATED_DEFAULT", "false"}});

  // Make sure this is set up right.
  EXPECT_THROW_WITH_REGEX(checkForDeprecation(base), Envoy::EnvoyException,
                          "Using the default now-deprecated value DEPRECATED_DEFAULT");

  // Verify that even when the enable_all_deprecated_features is enabled the
  // enum is disallowed.
  mergeValues({{"envoy.features.enable_all_deprecated_features", "true"}});

  EXPECT_THROW_WITH_REGEX(checkForDeprecation(base), Envoy::EnvoyException,
                          "Using the default now-deprecated value DEPRECATED_DEFAULT");
}

// Make sure the runtime overrides for allowing fatal enums work.
TEST_F(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(FatalEnum)) {
  envoy::test::deprecation_test::Base base;
  base.mutable_enum_container()->set_deprecated_enum(
      envoy::test::deprecation_test::Base::DEPRECATED_FATAL);
  EXPECT_THROW_WITH_REGEX(checkForDeprecation(base), Envoy::EnvoyException,
                          "Using deprecated value DEPRECATED_FATAL");

  mergeValues(
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
TEST_F(DeprecatedFieldsTest, DEPRECATED_FEATURE_TEST(FatalEnumGlobalOverride)) {
  envoy::test::deprecation_test::Base base;
  base.mutable_enum_container()->set_deprecated_enum(
      envoy::test::deprecation_test::Base::DEPRECATED_FATAL);
  EXPECT_THROW_WITH_REGEX(checkForDeprecation(base), Envoy::EnvoyException,
                          "Using deprecated value DEPRECATED_FATAL");

  mergeValues({{"envoy.features.enable_all_deprecated_features", "true"}});

  EXPECT_LOG_CONTAINS(
      "warning",
      "Using runtime overrides to continue using now fatal-by-default deprecated value "
      "DEPRECATED_FATAL for enum "
      "'envoy.test.deprecation_test.Base.InnerMessageWithDeprecationEnum.deprecated_enum' "
      "from file deprecated.proto. This enum value will be removed from Envoy soon.",
      checkForDeprecation(base));
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
  int last_code = static_cast<int>(absl::StatusCode::kUnauthenticated);
  for (int i = 0; i < last_code; ++i) {
    EXPECT_NE(MessageUtil::codeEnumToString(static_cast<absl::StatusCode>(i)), "");
  }
  ASSERT_EQ("UNKNOWN: ",
            MessageUtil::codeEnumToString(static_cast<absl::StatusCode>(last_code + 1)));
  ASSERT_EQ("OK", MessageUtil::codeEnumToString(absl::StatusCode::kOk));
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

TEST_F(ProtobufUtilityTest, SubsequentLoadClearsExistingProtoValues) {
  utility_test::message_field_wip::MultipleFields obj;
  MessageUtil::loadFromYaml("foo: bar\nbar: qux", obj, ProtobufMessage::getNullValidationVisitor());
  EXPECT_EQ(obj.foo(), "bar");
  EXPECT_EQ(obj.bar(), "qux");
  EXPECT_EQ(obj.baz(), 0);

  // Subsequent load into a proto with some existing values, should clear them up.
  MessageUtil::loadFromYaml("baz: 2", obj, ProtobufMessage::getNullValidationVisitor());
  EXPECT_TRUE(obj.foo().empty());
  EXPECT_TRUE(obj.bar().empty());
  EXPECT_EQ(obj.baz(), 2);
}

// Validate that Equals and Equivalent have the same behavior with respect to
// out of order repeated fields.
TEST_F(ProtobufUtilityTest, CompareRepeatedFields) {
  utility_test::message_field_wip::RepeatedField message1;
  utility_test::message_field_wip::RepeatedField same_order;
  utility_test::message_field_wip::RepeatedField different_order;

  utility_test::message_field_wip::MultipleFields element1;
  element1.set_foo("foo");
  element1.set_bar("bar");
  element1.set_baz(57);
  utility_test::message_field_wip::MultipleFields element2;
  element2.set_foo("foo1");
  element2.set_bar("bar1");
  element2.set_baz(25597);
  utility_test::message_field_wip::MultipleFields element3;
  element3.set_foo("foo99");
  element3.set_bar("678bar");
  element3.set_baz(985734);

  *message1.add_repeated_multiple_fields() = element1;
  *message1.add_repeated_multiple_fields() = element2;
  *message1.add_repeated_multiple_fields() = element3;

  *same_order.add_repeated_multiple_fields() = element1;
  *same_order.add_repeated_multiple_fields() = element2;
  *same_order.add_repeated_multiple_fields() = element3;

  // Swap element 2 and 3
  *different_order.add_repeated_multiple_fields() = element1;
  *different_order.add_repeated_multiple_fields() = element3;
  *different_order.add_repeated_multiple_fields() = element2;

  EXPECT_TRUE(Protobuf::util::MessageDifferencer::Equals(message1, same_order));
  EXPECT_FALSE(Protobuf::util::MessageDifferencer::Equals(message1, different_order));

  EXPECT_TRUE(Protobuf::util::MessageDifferencer::Equivalent(message1, same_order));
  EXPECT_FALSE(Protobuf::util::MessageDifferencer::Equivalent(message1, different_order));
}

// Validate that order of insertion into a map does not influence results of
// Equals and Equivalent calls.
TEST_F(ProtobufUtilityTest, CompareMapFieldsCpp) {
  utility_test::message_field_wip::MapField message1;
  utility_test::message_field_wip::MapField same_order;
  utility_test::message_field_wip::MapField different_order;

  (*message1.mutable_map_field())["foo"] = "bar";
  (*message1.mutable_map_field())["foo1"] = "bar1";
  (*message1.mutable_map_field())["foo2"] = "bar2";

  (*same_order.mutable_map_field())["foo"] = "bar";
  (*same_order.mutable_map_field())["foo1"] = "bar1";
  (*same_order.mutable_map_field())["foo2"] = "bar2";

  (*different_order.mutable_map_field())["foo"] = "bar";
  (*different_order.mutable_map_field())["foo2"] = "bar2";
  (*different_order.mutable_map_field())["foo1"] = "bar1";

  EXPECT_TRUE(Protobuf::util::MessageDifferencer::Equals(message1, same_order));
  EXPECT_TRUE(Protobuf::util::MessageDifferencer::Equals(message1, different_order));

  EXPECT_TRUE(Protobuf::util::MessageDifferencer::Equivalent(message1, same_order));
  EXPECT_TRUE(Protobuf::util::MessageDifferencer::Equivalent(message1, different_order));
}

// Validate that proto maps that were deserialized from wire representations with
// different orders still produce the same result in the Equals and Equivalent
// methods.
TEST_F(ProtobufUtilityTest, CompareMapFieldsWire) {
  utility_test::message_field_wip::StringMapWireCompatible::MapFieldEntry entry1;
  entry1.set_key("foo");
  entry1.set_value("bar");
  utility_test::message_field_wip::StringMapWireCompatible::MapFieldEntry entry2;
  entry2.set_key("foo1");
  entry2.set_value("bar1");
  utility_test::message_field_wip::StringMapWireCompatible::MapFieldEntry entry3;
  entry3.set_key("foo2");
  entry3.set_value("bar2");

  utility_test::message_field_wip::StringMapWireCompatible wire_map1;
  *wire_map1.add_entries() = entry1;
  *wire_map1.add_entries() = entry2;
  *wire_map1.add_entries() = entry3;
  std::string wire_bytes1;
  EXPECT_TRUE(wire_map1.SerializeToString(&wire_bytes1));

  utility_test::message_field_wip::StringMapWireCompatible wire_map2;
  *wire_map2.add_entries() = entry2;
  *wire_map2.add_entries() = entry3;
  *wire_map2.add_entries() = entry1;
  std::string wire_bytes2;
  EXPECT_TRUE(wire_map2.SerializeToString(&wire_bytes2));

  // The MapField and StringMapWireCompatible are wire compatible per
  // https://protobuf.dev/programming-guides/proto3/#backwards
  utility_test::message_field_wip::MapField message1;
  EXPECT_TRUE(message1.ParseFromString(wire_bytes1));
  EXPECT_EQ(message1.map_field_size(), 3);
  utility_test::message_field_wip::MapField same_order;
  EXPECT_TRUE(same_order.ParseFromString(wire_bytes1));
  // Parse different_order proto from wire bytes with elements in a different order from wire_bytes1
  utility_test::message_field_wip::MapField different_order;
  EXPECT_TRUE(different_order.ParseFromString(wire_bytes2));
  EXPECT_EQ(different_order.map_field_size(), 3);

  EXPECT_TRUE(Protobuf::util::MessageDifferencer::Equals(message1, same_order));
  EXPECT_TRUE(Protobuf::util::MessageDifferencer::Equals(message1, different_order));

  EXPECT_TRUE(Protobuf::util::MessageDifferencer::Equivalent(message1, same_order));
  EXPECT_TRUE(Protobuf::util::MessageDifferencer::Equivalent(message1, different_order));
}

} // namespace Envoy
