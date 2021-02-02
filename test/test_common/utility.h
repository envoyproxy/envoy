#pragma once

#include <cstdlib>
#include <list>
#include <random>
#include <string>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/buffer/buffer.h"
#include "envoy/network/address.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/store.h"
#include "envoy/thread/thread.h"
#include "envoy/type/matcher/v3/string.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/c_smart_ptr.h"
#include "common/common/empty_string.h"
#include "common/common/thread.h"
#include "common/config/decoded_resource_impl.h"
#include "common/config/opaque_resource_decoder_impl.h"
#include "common/config/version_converter.h"
#include "common/http/header_map_impl.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/protobuf/utility.h"
#include "common/stats/symbol_table_impl.h"

#include "test/test_common/file_system_for_test.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_time_system.h"
#include "test/test_common/thread_factory_for_test.h"

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_; // NOLINT(misc-unused-using-decls)
using testing::AssertionFailure;
using testing::AssertionResult;
using testing::AssertionSuccess;
using testing::Invoke; //  NOLINT(misc-unused-using-decls)

namespace Envoy {

/*
  Macro to use for validating that a statement throws the specified type of exception, and that
  the exception's what() method returns a string which is matched by the specified matcher.
  This allows for expectations such as:

  EXPECT_THAT_THROWS_MESSAGE(
      bad_function_call(),
      EnvoyException,
      AllOf(StartsWith("expected prefix"), HasSubstr("some substring")));
*/
#define EXPECT_THAT_THROWS_MESSAGE(statement, expected_exception, matcher)                         \
  try {                                                                                            \
    statement;                                                                                     \
    ADD_FAILURE() << "Exception should take place. It did not.";                                   \
  } catch (expected_exception & e) {                                                               \
    EXPECT_THAT(std::string(e.what()), matcher);                                                   \
  }

// Expect that the statement throws the specified type of exception with exactly the specified
// message.
#define EXPECT_THROW_WITH_MESSAGE(statement, expected_exception, message)                          \
  EXPECT_THAT_THROWS_MESSAGE(statement, expected_exception, ::testing::Eq(message))

// Expect that the statement throws the specified type of exception with a message containing a
// substring matching the specified regular expression (i.e. the regex doesn't have to match
// the entire message).
#define EXPECT_THROW_WITH_REGEX(statement, expected_exception, regex_str)                          \
  EXPECT_THAT_THROWS_MESSAGE(statement, expected_exception, ::testing::ContainsRegex(regex_str))

// Expect that the statement throws the specified type of exception with a message that does not
// contain any substring matching the specified regular expression.
#define EXPECT_THROW_WITHOUT_REGEX(statement, expected_exception, regex_str)                       \
  EXPECT_THAT_THROWS_MESSAGE(statement, expected_exception,                                        \
                             ::testing::Not(::testing::ContainsRegex(regex_str)))

#define VERBOSE_EXPECT_NO_THROW(statement)                                                         \
  try {                                                                                            \
    statement;                                                                                     \
  } catch (EnvoyException & e) {                                                                   \
    ADD_FAILURE() << "Unexpected exception: " << std::string(e.what());                            \
  }

#define VERIFY_ASSERTION(statement)                                                                \
  do {                                                                                             \
    ::testing::AssertionResult status = statement;                                                 \
    if (!status) {                                                                                 \
      return status;                                                                               \
    }                                                                                              \
  } while (false)

// A convenience macro for testing Envoy deprecated features. This will disable the test when
// tests are built with --define deprecated_features=disabled to avoid the hard-failure mode for
// deprecated features. Sample usage is:
//
// TEST_F(FixtureName, DEPRECATED_FEATURE_TEST(TestName)) {
// ...
// }
#ifndef ENVOY_DISABLE_DEPRECATED_FEATURES
#define DEPRECATED_FEATURE_TEST(X) X
#else
#define DEPRECATED_FEATURE_TEST(X) DISABLED_##X
#endif

// Random number generator which logs its seed to stderr. To repeat a test run with a non-zero seed
// one can run the test with --test_arg=--gtest_random_seed=[seed]
class TestRandomGenerator {
public:
  TestRandomGenerator();

  uint64_t random();

private:
  const int32_t seed_;
  std::ranlux48 generator_;
  RealTimeSource real_time_source_;
};

class TestUtility {
public:
  /**
   * Compare 2 HeaderMaps.
   * @param lhs supplies HeaderMap 1.
   * @param rhs supplies HeaderMap 2.
   * @return TRUE if the HeaderMaps are equal, ignoring the order of the
   * headers, false if not.
   */
  static bool headerMapEqualIgnoreOrder(const Http::HeaderMap& lhs, const Http::HeaderMap& rhs);

  /**
   * Compare 2 buffers.
   * @param lhs supplies buffer 1.
   * @param rhs supplies buffer 2.
   * @return TRUE if the buffers contain equal content
   *         (i.e., if lhs.toString() == rhs.toString()), false if not.
   */
  static bool buffersEqual(const Buffer::Instance& lhs, const Buffer::Instance& rhs);

  /**
   * Compare 2 RawSlice pointers.
   * @param lhs supplies raw slice 1.
   * @param rhs supplies raw slice 2.
   * @param num_slices The number of slices to compare. It is assumed lhs and rhs have the same
   * number.
   * @return true if for num_slices, all lhs raw slices are equal to the corresponding rhs raw slice
   *         in length and a byte by byte data comparison. false otherwise
   */
  static bool rawSlicesEqual(const Buffer::RawSlice* lhs, const Buffer::RawSlice* rhs,
                             size_t num_slices);

  /**
   * Feed a buffer with random characters.
   * @param buffer supplies the buffer to be fed.
   * @param n_char number of characters that should be added to the supplied buffer.
   * @param seed seeds pseudo-random number generator (default = 0).
   */
  static void feedBufferWithRandomCharacters(Buffer::Instance& buffer, uint64_t n_char,
                                             uint64_t seed = 0);

  /**
   * Finds a stat in a vector with the given name.
   * @param name the stat name to look for.
   * @param v the vector of stats.
   * @return the stat
   */
  template <typename T> static T findByName(const std::vector<T>& v, const std::string& name) {
    auto pos = std::find_if(v.begin(), v.end(),
                            [&name](const T& stat) -> bool { return stat->name() == name; });
    if (pos == v.end()) {
      return nullptr;
    }
    return *pos;
  }

  /**
   * Find a counter in a stats store.
   * @param store supplies the stats store.
   * @param name supplies the name to search for.
   * @return Stats::CounterSharedPtr the counter or nullptr if there is none.
   */
  static Stats::CounterSharedPtr findCounter(Stats::Store& store, const std::string& name);

  /**
   * Find a gauge in a stats store.
   * @param store supplies the stats store.
   * @param name supplies the name to search for.
   * @return Stats::GaugeSharedPtr the gauge or nullptr if there is none.
   */
  static Stats::GaugeSharedPtr findGauge(Stats::Store& store, const std::string& name);

  /**
   * Wait for a counter to == a given value.
   * @param store supplies the stats store.
   * @param name supplies the name of the counter to wait for.
   * @param value supplies the value of the counter.
   * @param time_system the time system to use for waiting.
   * @param timeout the maximum time to wait before timing out, or 0 for no timeout.
   * @param dispatcher the dispatcher to run non-blocking periodically during the wait.
   * @return AssertionSuccess() if the counter was == to the value within the timeout, else
   * AssertionFailure().
   */
  static AssertionResult
  waitForCounterEq(Stats::Store& store, const std::string& name, uint64_t value,
                   Event::TestTimeSystem& time_system,
                   std::chrono::milliseconds timeout = std::chrono::milliseconds::zero(),
                   Event::Dispatcher* dispatcher = nullptr);

  /**
   * Wait for a counter to >= a given value.
   * @param store supplies the stats store.
   * @param name counter name.
   * @param value target value.
   * @param time_system the time system to use for waiting.
   * @param timeout the maximum time to wait before timing out, or 0 for no timeout.
   * @return AssertionSuccess() if the counter was >= to the value within the timeout, else
   * AssertionFailure().
   */
  static AssertionResult
  waitForCounterGe(Stats::Store& store, const std::string& name, uint64_t value,
                   Event::TestTimeSystem& time_system,
                   std::chrono::milliseconds timeout = std::chrono::milliseconds::zero());

  /**
   * Wait for a gauge to >= a given value.
   * @param store supplies the stats store.
   * @param name gauge name.
   * @param value target value.
   * @param time_system the time system to use for waiting.
   * @param timeout the maximum time to wait before timing out, or 0 for no timeout.
   * @return AssertionSuccess() if the counter gauge >= to the value within the timeout, else
   * AssertionFailure().
   */
  static AssertionResult
  waitForGaugeGe(Stats::Store& store, const std::string& name, uint64_t value,
                 Event::TestTimeSystem& time_system,
                 std::chrono::milliseconds timeout = std::chrono::milliseconds::zero());

  /**
   * Wait for a gauge to == a given value.
   * @param store supplies the stats store.
   * @param name gauge name.
   * @param value target value.
   * @param time_system the time system to use for waiting.
   * @param timeout the maximum time to wait before timing out, or 0 for no timeout.
   * @return AssertionSuccess() if the gauge was == to the value within the timeout, else
   * AssertionFailure().
   */
  static AssertionResult
  waitForGaugeEq(Stats::Store& store, const std::string& name, uint64_t value,
                 Event::TestTimeSystem& time_system,
                 std::chrono::milliseconds timeout = std::chrono::milliseconds::zero());

  /**
   * Find a readout in a stats store.
   * @param store supplies the stats store.
   * @param name supplies the name to search for.
   * @return Stats::TextReadoutSharedPtr the readout or nullptr if there is none.
   */
  static Stats::TextReadoutSharedPtr findTextReadout(Stats::Store& store, const std::string& name);

  /**
   * Convert a string list of IP addresses into a list of network addresses usable for DNS
   * response testing.
   */
  static std::list<Network::DnsResponse>
  makeDnsResponse(const std::list<std::string>& addresses,
                  std::chrono::seconds = std::chrono::seconds(0));

  /**
   * List files in a given directory path
   *
   * @param path directory path to list
   * @param recursive whether or not to traverse subdirectories
   * @return std::vector<std::string> filenames
   */
  static std::vector<std::string> listFiles(const std::string& path, bool recursive);

  /**
   * Return a unique temporary filename for use in tests.
   *
   * @return a filename based on the process id and current time.
   */

  static std::string uniqueFilename() {
    return absl::StrCat(getpid(), "_", std::chrono::system_clock::now().time_since_epoch().count());
  }

  /**
   * Compare two protos of the same type for equality.
   *
   * @param lhs proto on LHS.
   * @param rhs proto on RHS.
   * @param ignore_repeated_field_ordering if true, repeated field ordering will be ignored.
   * @return bool indicating whether the protos are equal.
   */
  static bool protoEqual(const Protobuf::Message& lhs, const Protobuf::Message& rhs,
                         bool ignore_repeated_field_ordering = false) {
    Protobuf::util::MessageDifferencer differencer;
    differencer.set_message_field_comparison(Protobuf::util::MessageDifferencer::EQUIVALENT);
    if (ignore_repeated_field_ordering) {
      differencer.set_repeated_field_comparison(Protobuf::util::MessageDifferencer::AS_SET);
    }
    return differencer.Compare(lhs, rhs);
  }

  static bool protoEqualIgnoringField(const Protobuf::Message& lhs, const Protobuf::Message& rhs,
                                      const std::string& field_to_ignore) {
    Protobuf::util::MessageDifferencer differencer;
    const Protobuf::FieldDescriptor* ignored_field =
        lhs.GetDescriptor()->FindFieldByName(field_to_ignore);
    ASSERT(ignored_field != nullptr, "Field name to ignore not found.");
    differencer.IgnoreField(ignored_field);
    return differencer.Compare(lhs, rhs);
  }

  /**
   * Compare two decoded resources for equality.
   *
   * @param lhs decoded resource on LHS.
   * @param rhs decoded resource on RHS.
   * @return bool indicating whether the decoded resources are equal.
   */
  static bool decodedResourceEq(const Config::DecodedResource& lhs,
                                const Config::DecodedResource& rhs) {
    return lhs.name() == rhs.name() && lhs.aliases() == rhs.aliases() &&
           lhs.version() == rhs.version() && lhs.hasResource() == rhs.hasResource() &&
           (!lhs.hasResource() || protoEqual(lhs.resource(), rhs.resource()));
  }

  /**
   * Compare two JSON strings serialized from ProtobufWkt::Struct for equality. When two identical
   * ProtobufWkt::Struct are serialized into JSON strings, the results have the same set of
   * properties (values), but the positions may be different.
   *
   * @param lhs JSON string on LHS.
   * @param rhs JSON string on RHS.
   * @return bool indicating whether the JSON strings are equal.
   */
  static bool jsonStringEqual(const std::string& lhs, const std::string& rhs) {
    return protoEqual(jsonToStruct(lhs), jsonToStruct(rhs));
  }

  /**
   * Symmetrically pad a string with '=' out to a desired length.
   * @param to_pad the string being padded around.
   * @param desired_length the length we want the padding to bring the string up to.
   * @return the padded string.
   */
  static std::string addLeftAndRightPadding(absl::string_view to_pad, int desired_length = 80);

  /**
   * Split a string.
   * @param source supplies the string to split.
   * @param split supplies the char to split on.
   * @return vector of strings computed after splitting `source` around all instances of `split`.
   */
  static std::vector<std::string> split(const std::string& source, char split);

  /**
   * Split a string.
   * @param source supplies the string to split.
   * @param split supplies the string to split on.
   * @param keep_empty_string result contains empty strings if the string starts or ends with
   * 'split', or if instances of 'split' are adjacent.
   * @return vector of strings computed after splitting `source` around all instances of `split`.
   */
  static std::vector<std::string> split(const std::string& source, const std::string& split,
                                        bool keep_empty_string = false);

  /**
   * Compare two RepeatedPtrFields of the same type for equality.
   *
   * @param lhs RepeatedPtrField on LHS.
   * @param rhs RepeatedPtrField on RHS.
   * @param ignore_ordering if ordering should be ignored. Note if true this turns
   *   comparison into an N^2 operation.
   * @return bool indicating whether the RepeatedPtrField are equal. TestUtility::protoEqual() is
   *              used for individual element testing.
   */
  template <typename ProtoType>
  static bool repeatedPtrFieldEqual(const Protobuf::RepeatedPtrField<ProtoType>& lhs,
                                    const Protobuf::RepeatedPtrField<ProtoType>& rhs,
                                    bool ignore_ordering = false) {
    if (lhs.size() != rhs.size()) {
      return false;
    }

    if (!ignore_ordering) {
      for (int i = 0; i < lhs.size(); ++i) {
        if (!TestUtility::protoEqual(lhs[i], rhs[i], /*ignore_ordering=*/false)) {
          return false;
        }
      }

      return true;
    }
    using ProtoList = std::list<std::unique_ptr<const Protobuf::Message>>;
    // Iterate through using protoEqual as ignore_ordering is true, and fields
    // in the sub-protos may also be out of order.
    ProtoList lhs_list =
        RepeatedPtrUtil::convertToConstMessagePtrContainer<ProtoType, ProtoList>(lhs);
    ProtoList rhs_list =
        RepeatedPtrUtil::convertToConstMessagePtrContainer<ProtoType, ProtoList>(rhs);
    while (!lhs_list.empty()) {
      bool found = false;
      for (auto it = rhs_list.begin(); it != rhs_list.end(); ++it) {
        if (TestUtility::protoEqual(*lhs_list.front(), **it,
                                    /*ignore_ordering=*/true)) {
          lhs_list.pop_front();
          rhs_list.erase(it);
          found = true;
          break;
        }
      }
      if (!found) {
        return false;
      }
    }
    return true;
  }

  template <class ProtoType>
  static AssertionResult
  assertRepeatedPtrFieldEqual(const Protobuf::RepeatedPtrField<ProtoType>& lhs,
                              const Protobuf::RepeatedPtrField<ProtoType>& rhs,
                              bool ignore_ordering = false) {
    if (!repeatedPtrFieldEqual(lhs, rhs, ignore_ordering)) {
      return AssertionFailure() << RepeatedPtrUtil::debugString(lhs) << " does not match "
                                << RepeatedPtrUtil::debugString(rhs);
    }

    return AssertionSuccess();
  }

  /**
   * Returns a "novel" IPv4 loopback address, if available.
   * For many tests, we want a loopback address other than 127.0.0.1 where possible. For some
   * platforms such as macOS, only 127.0.0.1 is available for IPv4 loopback.
   *
   * @return string 127.0.0.x , where x is "1" for macOS and "9" otherwise.
   */
  static std::string getIpv4Loopback() {
#ifdef __APPLE__
    return "127.0.0.1";
#else
    return "127.0.0.9";
#endif
  }

  /**
   * Return typed proto message object for YAML.
   * @param yaml YAML string.
   * @return MessageType parsed from yaml.
   */
  template <class MessageType> static MessageType parseYaml(const std::string& yaml) {
    MessageType message;
    TestUtility::loadFromYaml(yaml, message);
    return message;
  }

  // Allows pretty printed test names for TEST_P using TestEnvironment::getIpVersionsForTest().
  //
  // Tests using this will be of the form IpVersions/SslSocketTest.HalfClose/IPv4
  // instead of IpVersions/SslSocketTest.HalfClose/1
  static std::string
  ipTestParamsToString(const ::testing::TestParamInfo<Network::Address::IpVersion>& params) {
    return params.param == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6";
  }

  /**
   * Return flip-ordered bytes.
   * @param bytes input bytes.
   * @return Type flip-ordered bytes.
   */
  template <class Type> static Type flipOrder(const Type& bytes) {
    Type result{0};
    Type data = bytes;
    for (Type i = 0; i < sizeof(Type); i++) {
      result <<= 8;
      result |= (data & Type(0xFF));
      data >>= 8;
    }
    return result;
  }

  static absl::Time parseTime(const std::string& input, const std::string& input_format);
  static std::string formatTime(const absl::Time input, const std::string& output_format);
  static std::string formatTime(const SystemTime input, const std::string& output_format);
  static std::string convertTime(const std::string& input, const std::string& input_format,
                                 const std::string& output_format);

  static constexpr std::chrono::milliseconds DefaultTimeout = std::chrono::milliseconds(10000);

  /**
   * Return a prefix string matcher.
   * @param string prefix.
   * @return Object StringMatcher.
   */
  static const envoy::type::matcher::v3::StringMatcher createPrefixMatcher(std::string str) {
    envoy::type::matcher::v3::StringMatcher matcher;
    matcher.set_prefix(str);
    return matcher;
  }

  /**
   * Return an exact string matcher.
   * @param string exact.
   * @return Object StringMatcher.
   */
  static const envoy::type::matcher::v3::StringMatcher createExactMatcher(std::string str) {
    envoy::type::matcher::v3::StringMatcher matcher;
    matcher.set_exact(str);
    return matcher;
  }

  /**
   * Return a regex string matcher.
   * @param string exact.
   * @return Object StringMatcher.
   */
  static const envoy::type::matcher::v3::StringMatcher createRegexMatcher(std::string str) {
    envoy::type::matcher::v3::StringMatcher matcher;
    matcher.set_hidden_envoy_deprecated_regex(str);
    return matcher;
  }

  /**
   * Checks that passed gauges have a value of 0. Gauges can be omitted from
   * this check by modifying the regex that matches gauge names in the
   * implementation.
   *
   * @param vector of gauges to check.
   * @return bool indicating that passed gauges not matching the omitted regex have a value of 0.
   */
  static bool gaugesZeroed(const std::vector<Stats::GaugeSharedPtr>& gauges);
  static bool gaugesZeroed(
      const std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>>& gauges);

  /**
   * Returns the members of gauges that are not zero. Uses the same regex filter as gaugesZeroed().
   */
  static std::string nonZeroedGauges(const std::vector<Stats::GaugeSharedPtr>& gauges);

  // Strict variants of Protobuf::MessageUtil
  static void loadFromJson(const std::string& json, Protobuf::Message& message,
                           bool preserve_original_type = false, bool avoid_boosting = false) {
    MessageUtil::loadFromJson(json, message, ProtobufMessage::getStrictValidationVisitor(),
                              !avoid_boosting);
    if (!preserve_original_type) {
      Config::VersionConverter::eraseOriginalTypeInformation(message);
    }
  }

  static void loadFromJson(const std::string& json, ProtobufWkt::Struct& message) {
    MessageUtil::loadFromJson(json, message);
  }

  static void loadFromYaml(const std::string& yaml, Protobuf::Message& message,
                           bool preserve_original_type = false, bool avoid_boosting = false) {
    MessageUtil::loadFromYaml(yaml, message, ProtobufMessage::getStrictValidationVisitor(),
                              !avoid_boosting);
    if (!preserve_original_type) {
      Config::VersionConverter::eraseOriginalTypeInformation(message);
    }
  }

  static void loadFromFile(const std::string& path, Protobuf::Message& message, Api::Api& api,
                           bool preserve_original_type = false) {
    MessageUtil::loadFromFile(path, message, ProtobufMessage::getStrictValidationVisitor(), api);
    if (!preserve_original_type) {
      Config::VersionConverter::eraseOriginalTypeInformation(message);
    }
  }

  template <class MessageType>
  static inline MessageType anyConvert(const ProtobufWkt::Any& message) {
    return MessageUtil::anyConvert<MessageType>(message);
  }

  template <class MessageType>
  static void loadFromYamlAndValidate(const std::string& yaml, MessageType& message,
                                      bool preserve_original_type = false,
                                      bool avoid_boosting = false) {
    MessageUtil::loadFromYamlAndValidate(
        yaml, message, ProtobufMessage::getStrictValidationVisitor(), avoid_boosting);
    if (!preserve_original_type) {
      Config::VersionConverter::eraseOriginalTypeInformation(message);
    }
  }

  template <class MessageType> static void validate(const MessageType& message) {
    MessageUtil::validate(message, ProtobufMessage::getStrictValidationVisitor());
  }

  template <class MessageType>
  static const MessageType& downcastAndValidate(const Protobuf::Message& config) {
    return MessageUtil::downcastAndValidate<MessageType>(
        config, ProtobufMessage::getStrictValidationVisitor());
  }

  static void jsonConvert(const Protobuf::Message& source, Protobuf::Message& dest) {
    // Explicit round-tripping to support conversions inside tests between arbitrary messages as a
    // convenience.
    ProtobufWkt::Struct tmp;
    MessageUtil::jsonConvert(source, tmp);
    MessageUtil::jsonConvert(tmp, ProtobufMessage::getStrictValidationVisitor(), dest);
  }

  static ProtobufWkt::Struct jsonToStruct(const std::string& json) {
    ProtobufWkt::Struct message;
    MessageUtil::loadFromJson(json, message);
    return message;
  }

  /**
   * Extract the Protobuf binary format of a google.protobuf.Message as a string.
   * @param message message of type type.googleapis.com/google.protobuf.Message.
   * @return std::string of the Protobuf binary object.
   */
  static std::string getProtobufBinaryStringFromMessage(const Protobuf::Message& message) {
    std::string pb_binary_str;
    pb_binary_str.reserve(message.ByteSizeLong());
    message.SerializeToString(&pb_binary_str);
    return pb_binary_str;
  }

  template <class MessageType>
  static Config::DecodedResourcesWrapper
  decodeResources(std::initializer_list<MessageType> resources,
                  const std::string& name_field = "name") {
    Config::DecodedResourcesWrapper decoded_resources;
    for (const auto& resource : resources) {
      auto owned_resource = std::make_unique<MessageType>(resource);
      decoded_resources.owned_resources_.emplace_back(new Config::DecodedResourceImpl(
          std::move(owned_resource), MessageUtil::getStringField(resource, name_field), {}, ""));
      decoded_resources.refvec_.emplace_back(*decoded_resources.owned_resources_.back());
    }
    return decoded_resources;
  }

  template <class MessageType>
  static Config::DecodedResourcesWrapper decodeResources(std::vector<MessageType> resources,
                                                         const std::string& name_field = "name") {
    Config::DecodedResourcesWrapper decoded_resources;
    for (const auto& resource : resources) {
      auto owned_resource = std::make_unique<MessageType>(resource);
      decoded_resources.owned_resources_.emplace_back(new Config::DecodedResourceImpl(
          std::move(owned_resource), MessageUtil::getStringField(resource, name_field), {}, ""));
      decoded_resources.refvec_.emplace_back(*decoded_resources.owned_resources_.back());
    }
    return decoded_resources;
  }

  template <class MessageType>
  static Config::DecodedResourcesWrapper
  decodeResources(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                  const std::string& version, const std::string& name_field = "name") {
    TestOpaqueResourceDecoderImpl<MessageType> resource_decoder(name_field);
    return Config::DecodedResourcesWrapper(resource_decoder, resources, version);
  }

  template <class MessageType>
  static Config::DecodedResourcesWrapper
  decodeResources(const envoy::service::discovery::v3::DiscoveryResponse& resources,
                  const std::string& name_field = "name") {
    return decodeResources<MessageType>(resources.resources(), resources.version_info(),
                                        name_field);
  }

  template <class MessageType>
  static Config::DecodedResourcesWrapper decodeResources(
      const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& resources,
      const std::string& name_field = "name") {
    Config::DecodedResourcesWrapper decoded_resources;
    TestOpaqueResourceDecoderImpl<MessageType> resource_decoder(name_field);
    for (const auto& resource : resources) {
      decoded_resources.owned_resources_.emplace_back(
          new Config::DecodedResourceImpl(resource_decoder, resource));
      decoded_resources.refvec_.emplace_back(*decoded_resources.owned_resources_.back());
    }
    return decoded_resources;
  }

  template <typename Current>
  class TestOpaqueResourceDecoderImpl : public Config::OpaqueResourceDecoderImpl<Current> {
  public:
    TestOpaqueResourceDecoderImpl(absl::string_view name_field)
        : Config::OpaqueResourceDecoderImpl<Current>(ProtobufMessage::getStrictValidationVisitor(),
                                                     name_field) {}
  };

  /**
   * Returns the string representation of a envoy::config::core::v3::ApiVersion.
   *
   * @param api_version to be converted.
   * @return std::string representation of envoy::config::core::v3::ApiVersion.
   */
  static std::string
  getVersionStringFromApiVersion(envoy::config::core::v3::ApiVersion api_version) {
    switch (api_version) {
    case envoy::config::core::v3::ApiVersion::AUTO:
      return "AUTO";
    case envoy::config::core::v3::ApiVersion::V2:
      return "V2";
    case envoy::config::core::v3::ApiVersion::V3:
      return "V3";
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  /**
   * Returns the fully-qualified name of a service, rendered from service_full_name_template.
   *
   * @param service_full_name_template the service fully-qualified name template.
   * @param api_version version of a service.
   * @param use_alpha if the alpha version is preferred.
   * @param service_namespace to override the service namespace.
   * @return std::string full path of a service method.
   */
  static std::string
  getVersionedServiceFullName(const std::string& service_full_name_template,
                              envoy::config::core::v3::ApiVersion api_version,
                              bool use_alpha = false,
                              const std::string& service_namespace = EMPTY_STRING) {
    switch (api_version) {
    case envoy::config::core::v3::ApiVersion::AUTO:
      FALLTHRU;
    case envoy::config::core::v3::ApiVersion::V2:
      return fmt::format(service_full_name_template, use_alpha ? "v2alpha" : "v2",
                         service_namespace);

    case envoy::config::core::v3::ApiVersion::V3:
      return fmt::format(service_full_name_template, "v3", service_namespace);
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  /**
   * Returns the full path of a service method.
   *
   * @param service_full_name_template the service fully-qualified name template.
   * @param method_name the method name.
   * @param api_version version of a service method.
   * @param use_alpha if the alpha version is preferred.
   * @param service_namespace to override the service namespace.
   * @return std::string full path of a service method.
   */
  static std::string getVersionedMethodPath(const std::string& service_full_name_template,
                                            absl::string_view method_name,
                                            envoy::config::core::v3::ApiVersion api_version,
                                            bool use_alpha = false,
                                            const std::string& service_namespace = EMPTY_STRING) {
    return absl::StrCat("/",
                        getVersionedServiceFullName(service_full_name_template, api_version,
                                                    use_alpha, service_namespace),
                        "/", method_name);
  }
};

/**
 * Wraps the common case of having a cross-thread "one shot" ready condition.
 *
 * It functions like absl::Notification except the usage of notifyAll() appears
 * to trigger tighter simultaneous wakeups in multiple threads, resulting in
 * more contentions, e.g. for BM_CreateRace in
 * ../common/stats/symbol_table_speed_test.cc.
 *
 * See
 *     https://github.com/abseil/abseil-cpp/blob/master/absl/synchronization/notification.h
 * for the absl impl, which appears to result in fewer contentions (and in
 * tests we want contentions).
 */
class ConditionalInitializer {
public:
  /**
   * Set the conditional to ready.
   */
  void setReady();

  /**
   * Block until the conditional is ready, will return immediately if it is already ready. This
   * routine will also reset ready_ so that the initializer can be used again. setReady() should
   * only be called once in between a call to waitReady().
   */
  void waitReady();

  /**
   * Waits until ready; does not reset it. This variation is immune to spurious
   * condvar wakeups, and is also suitable for having multiple threads wait on
   * a common condition.
   */
  void wait();

private:
  absl::Mutex mutex_;
  bool ready_ ABSL_GUARDED_BY(mutex_){false};
};

namespace Http {

/**
 * All of the inline header functions that just pass through to the child header map.
 */
#define DEFINE_TEST_INLINE_HEADER_FUNCS(name)                                                      \
  const HeaderEntry* name() const override { return header_map_->name(); }                         \
  size_t remove##name() override {                                                                 \
    const size_t headers_removed = header_map_->remove##name();                                    \
    header_map_->verifyByteSizeInternalForTest();                                                  \
    return headers_removed;                                                                        \
  }                                                                                                \
  absl::string_view get##name##Value() const override { return header_map_->get##name##Value(); }  \
  void set##name(absl::string_view value) override {                                               \
    header_map_->set##name(value);                                                                 \
    header_map_->verifyByteSizeInternalForTest();                                                  \
  }

#define DEFINE_TEST_INLINE_STRING_HEADER_FUNCS(name)                                               \
public:                                                                                            \
  DEFINE_TEST_INLINE_HEADER_FUNCS(name)                                                            \
  void append##name(absl::string_view data, absl::string_view delimiter) override {                \
    header_map_->append##name(data, delimiter);                                                    \
    header_map_->verifyByteSizeInternalForTest();                                                  \
  }                                                                                                \
  void setReference##name(absl::string_view value) override {                                      \
    header_map_->setReference##name(value);                                                        \
    header_map_->verifyByteSizeInternalForTest();                                                  \
  }

#define DEFINE_TEST_INLINE_NUMERIC_HEADER_FUNCS(name)                                              \
public:                                                                                            \
  DEFINE_TEST_INLINE_HEADER_FUNCS(name)                                                            \
  void set##name(uint64_t value) override {                                                        \
    header_map_->set##name(value);                                                                 \
    header_map_->verifyByteSizeInternalForTest();                                                  \
  }

/**
 * Base class for all test header map types. This class wraps an underlying real header map
 * implementation, passes through all calls, and adds some niceties for testing that we don't
 * want in the production implementation for performance reasons. The wrapping functionality is
 * primarily here to deal with complexities around virtual calls in some constructor paths in
 * HeaderMapImpl.
 */
template <class Interface, class Impl> class TestHeaderMapImplBase : public Interface {
public:
  TestHeaderMapImplBase() = default;
  TestHeaderMapImplBase(const std::initializer_list<std::pair<std::string, std::string>>& values) {
    for (auto& value : values) {
      header_map_->addCopy(LowerCaseString(value.first), value.second);
    }
    header_map_->verifyByteSizeInternalForTest();
  }
  TestHeaderMapImplBase(const TestHeaderMapImplBase& rhs)
      : TestHeaderMapImplBase(*rhs.header_map_) {}
  TestHeaderMapImplBase(const HeaderMap& rhs) {
    HeaderMapImpl::copyFrom(*header_map_, rhs);
    header_map_->verifyByteSizeInternalForTest();
  }
  void copyFrom(const TestHeaderMapImplBase& rhs) { copyFrom(*rhs.header_map_); }
  void copyFrom(const HeaderMap& rhs) {
    HeaderMapImpl::copyFrom(*header_map_, rhs);
    header_map_->verifyByteSizeInternalForTest();
  }
  TestHeaderMapImplBase& operator=(const TestHeaderMapImplBase& rhs) {
    if (this == &rhs) {
      return *this;
    }
    clear();
    HeaderMapImpl::copyFrom(*header_map_, rhs);
    header_map_->verifyByteSizeInternalForTest();
    return *this;
  }

  // Value added methods on top of HeaderMap.
  void addCopy(const std::string& key, const std::string& value) {
    addCopy(LowerCaseString(key), value);
  }
  std::string get_(const std::string& key) const { return get_(LowerCaseString(key)); }
  std::string get_(const LowerCaseString& key) const {
    // TODO(mattklein123): Possibly allow getting additional headers beyond the first.
    auto headers = get(key);
    if (headers.empty()) {
      return EMPTY_STRING;
    } else {
      return std::string(headers[0]->value().getStringView());
    }
  }
  bool has(const std::string& key) const { return !get(LowerCaseString(key)).empty(); }
  bool has(const LowerCaseString& key) const { return !get(key).empty(); }
  size_t remove(const std::string& key) { return remove(LowerCaseString(key)); }

  // HeaderMap
  bool operator==(const HeaderMap& rhs) const override { return header_map_->operator==(rhs); }
  bool operator!=(const HeaderMap& rhs) const override { return header_map_->operator!=(rhs); }
  void addViaMove(HeaderString&& key, HeaderString&& value) override {
    header_map_->addViaMove(std::move(key), std::move(value));
    header_map_->verifyByteSizeInternalForTest();
  }
  void addReference(const LowerCaseString& key, absl::string_view value) override {
    header_map_->addReference(key, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void addReferenceKey(const LowerCaseString& key, uint64_t value) override {
    header_map_->addReferenceKey(key, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void addReferenceKey(const LowerCaseString& key, absl::string_view value) override {
    header_map_->addReferenceKey(key, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void addCopy(const LowerCaseString& key, uint64_t value) override {
    header_map_->addCopy(key, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void addCopy(const LowerCaseString& key, absl::string_view value) override {
    header_map_->addCopy(key, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void appendCopy(const LowerCaseString& key, absl::string_view value) override {
    header_map_->appendCopy(key, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void setReference(const LowerCaseString& key, absl::string_view value) override {
    header_map_->setReference(key, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void setReferenceKey(const LowerCaseString& key, absl::string_view value) override {
    header_map_->setReferenceKey(key, value);
  }
  void setCopy(const LowerCaseString& key, absl::string_view value) override {
    header_map_->setCopy(key, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  uint64_t byteSize() const override { return header_map_->byteSize(); }
  HeaderMap::GetResult get(const LowerCaseString& key) const override {
    return header_map_->get(key);
  }
  void iterate(HeaderMap::ConstIterateCb cb) const override { header_map_->iterate(cb); }
  void iterateReverse(HeaderMap::ConstIterateCb cb) const override {
    header_map_->iterateReverse(cb);
  }
  void clear() override {
    header_map_->clear();
    header_map_->verifyByteSizeInternalForTest();
  }
  size_t remove(const LowerCaseString& key) override {
    size_t headers_removed = header_map_->remove(key);
    header_map_->verifyByteSizeInternalForTest();
    return headers_removed;
  }
  size_t removeIf(const HeaderMap::HeaderMatchPredicate& predicate) override {
    size_t headers_removed = header_map_->removeIf(predicate);
    header_map_->verifyByteSizeInternalForTest();
    return headers_removed;
  }
  size_t removePrefix(const LowerCaseString& key) override {
    size_t headers_removed = header_map_->removePrefix(key);
    header_map_->verifyByteSizeInternalForTest();
    return headers_removed;
  }
  size_t size() const override { return header_map_->size(); }
  bool empty() const override { return header_map_->empty(); }
  void dumpState(std::ostream& os, int indent_level = 0) const override {
    header_map_->dumpState(os, indent_level);
  }

  using Handle = typename CustomInlineHeaderRegistry::Handle<Interface::header_map_type>;
  const HeaderEntry* getInline(Handle handle) const override {
    return header_map_->getInline(handle);
  }
  void appendInline(Handle handle, absl::string_view data, absl::string_view delimiter) override {
    header_map_->appendInline(handle, data, delimiter);
    header_map_->verifyByteSizeInternalForTest();
  }
  void setReferenceInline(Handle handle, absl::string_view value) override {
    header_map_->setReferenceInline(handle, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void setInline(Handle handle, absl::string_view value) override {
    header_map_->setInline(handle, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void setInline(Handle handle, uint64_t value) override {
    header_map_->setInline(handle, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  size_t removeInline(Handle handle) override {
    const size_t rc = header_map_->removeInline(handle);
    header_map_->verifyByteSizeInternalForTest();
    return rc;
  }

  std::unique_ptr<Impl> header_map_{Impl::create()};
};

/**
 * Typed test implementations for all of the concrete header types.
 */
class TestRequestHeaderMapImpl
    : public TestHeaderMapImplBase<RequestHeaderMap, RequestHeaderMapImpl> {
public:
  using TestHeaderMapImplBase::TestHeaderMapImplBase;

  INLINE_REQ_STRING_HEADERS(DEFINE_TEST_INLINE_STRING_HEADER_FUNCS)
  INLINE_REQ_NUMERIC_HEADERS(DEFINE_TEST_INLINE_NUMERIC_HEADER_FUNCS)
  INLINE_REQ_RESP_STRING_HEADERS(DEFINE_TEST_INLINE_STRING_HEADER_FUNCS)
  INLINE_REQ_RESP_NUMERIC_HEADERS(DEFINE_TEST_INLINE_NUMERIC_HEADER_FUNCS)
};

using TestRequestTrailerMapImpl = TestHeaderMapImplBase<RequestTrailerMap, RequestTrailerMapImpl>;

class TestResponseHeaderMapImpl
    : public TestHeaderMapImplBase<ResponseHeaderMap, ResponseHeaderMapImpl> {
public:
  using TestHeaderMapImplBase::TestHeaderMapImplBase;

  INLINE_RESP_STRING_HEADERS(DEFINE_TEST_INLINE_STRING_HEADER_FUNCS)
  INLINE_RESP_NUMERIC_HEADERS(DEFINE_TEST_INLINE_NUMERIC_HEADER_FUNCS)
  INLINE_REQ_RESP_STRING_HEADERS(DEFINE_TEST_INLINE_STRING_HEADER_FUNCS)
  INLINE_REQ_RESP_NUMERIC_HEADERS(DEFINE_TEST_INLINE_NUMERIC_HEADER_FUNCS)
  INLINE_RESP_STRING_HEADERS_TRAILERS(DEFINE_TEST_INLINE_STRING_HEADER_FUNCS)
  INLINE_RESP_NUMERIC_HEADERS_TRAILERS(DEFINE_TEST_INLINE_NUMERIC_HEADER_FUNCS)
};

class TestResponseTrailerMapImpl
    : public TestHeaderMapImplBase<ResponseTrailerMap, ResponseTrailerMapImpl> {
public:
  using TestHeaderMapImplBase::TestHeaderMapImplBase;

  INLINE_RESP_STRING_HEADERS_TRAILERS(DEFINE_TEST_INLINE_STRING_HEADER_FUNCS)
  INLINE_RESP_NUMERIC_HEADERS_TRAILERS(DEFINE_TEST_INLINE_NUMERIC_HEADER_FUNCS)
};

// Helper method to create a header map from an initializer list. Useful due to make_unique's
// inability to infer the initializer list type.
template <class T>
inline std::unique_ptr<T>
makeHeaderMap(const std::initializer_list<std::pair<std::string, std::string>>& values) {
  return std::make_unique<T, const std::initializer_list<std::pair<std::string, std::string>>&>(
      values);
}

} // namespace Http

namespace Api {
ApiPtr createApiForTest();
ApiPtr createApiForTest(Filesystem::Instance& filesystem);
ApiPtr createApiForTest(Random::RandomGenerator& random);
ApiPtr createApiForTest(Stats::Store& stat_store);
ApiPtr createApiForTest(Stats::Store& stat_store, Random::RandomGenerator& random);
ApiPtr createApiForTest(Event::TimeSystem& time_system);
ApiPtr createApiForTest(Stats::Store& stat_store, Event::TimeSystem& time_system);
} // namespace Api

MATCHER_P(HeaderMapEqualIgnoreOrder, expected, "") {
  const bool equal = TestUtility::headerMapEqualIgnoreOrder(*arg, *expected);
  if (!equal) {
    *result_listener << "\n"
                     << TestUtility::addLeftAndRightPadding("Expected header map:") << "\n"
                     << *expected
                     << TestUtility::addLeftAndRightPadding("is not equal to actual header map:")
                     << "\n"
                     << *arg << TestUtility::addLeftAndRightPadding("") // line full of padding
                     << "\n";
  }
  return equal;
}

MATCHER_P(ProtoEq, expected, "") {
  const bool equal =
      TestUtility::protoEqual(arg, expected, /*ignore_repeated_field_ordering=*/false);
  if (!equal) {
    *result_listener << "\n"
                     << "==========================Expected proto:===========================\n"
                     << expected.DebugString()
                     << "------------------is not equal to actual proto:---------------------\n"
                     << arg.DebugString()
                     << "====================================================================\n";
  }
  return equal;
}

MATCHER_P(ProtoEqIgnoreRepeatedFieldOrdering, expected, "") {
  const bool equal =
      TestUtility::protoEqual(arg, expected, /*ignore_repeated_field_ordering=*/true);
  if (!equal) {
    *result_listener << "\n"
                     << TestUtility::addLeftAndRightPadding("Expected proto:") << "\n"
                     << expected.DebugString()
                     << TestUtility::addLeftAndRightPadding("is not equal to actual proto:") << "\n"
                     << arg.DebugString()
                     << TestUtility::addLeftAndRightPadding("") // line full of padding
                     << "\n";
  }
  return equal;
}

MATCHER_P2(ProtoEqIgnoringField, expected, ignored_field, "") {
  const bool equal = TestUtility::protoEqualIgnoringField(arg, expected, ignored_field);
  if (!equal) {
    std::string but_ignoring = absl::StrCat("(but ignoring ", ignored_field, ")");
    *result_listener << "\n"
                     << TestUtility::addLeftAndRightPadding("Expected proto:") << "\n"
                     << TestUtility::addLeftAndRightPadding(but_ignoring) << "\n"
                     << expected.DebugString()
                     << TestUtility::addLeftAndRightPadding("is not equal to actual proto:") << "\n"
                     << arg.DebugString()
                     << TestUtility::addLeftAndRightPadding("") // line full of padding
                     << "\n";
  }
  return equal;
}

MATCHER_P(RepeatedProtoEq, expected, "") {
  const bool equal = TestUtility::repeatedPtrFieldEqual(arg, expected);
  if (!equal) {
    *result_listener << "\n"
                     << TestUtility::addLeftAndRightPadding("Expected repeated:") << "\n"
                     << RepeatedPtrUtil::debugString(expected) << "\n"
                     << TestUtility::addLeftAndRightPadding("is not equal to actual repeated:")
                     << "\n"
                     << RepeatedPtrUtil::debugString(arg) << "\n"
                     << TestUtility::addLeftAndRightPadding("") // line full of padding
                     << "\n";
  }
  return equal;
}

MATCHER_P(DecodedResourcesEq, expected, "") {
  const bool equal = std::equal(arg.begin(), arg.end(), expected.begin(), expected.end(),
                                TestUtility::decodedResourceEq);
  if (!equal) {
    const auto format_resources =
        [](const std::vector<Config::DecodedResourceRef>& resources) -> std::string {
      std::vector<std::string> resource_strs;
      std::transform(
          resources.begin(), resources.end(), std::back_inserter(resource_strs),
          [](const Config::DecodedResourceRef& resource) -> std::string {
            return fmt::format(
                "<name: {}, aliases: {}, version: {}, resource: {}>", resource.get().name(),
                absl::StrJoin(resource.get().aliases(), ","), resource.get().version(),
                resource.get().hasResource() ? resource.get().resource().DebugString() : "(none)");
          });
      return absl::StrJoin(resource_strs, ", ");
    };
    *result_listener << "\n"
                     << TestUtility::addLeftAndRightPadding("Expected resources:") << "\n"
                     << format_resources(expected) << "\n"
                     << TestUtility::addLeftAndRightPadding("are not equal to actual resources:")
                     << "\n"
                     << format_resources(arg) << "\n"
                     << TestUtility::addLeftAndRightPadding("") // line full of padding
                     << "\n";
  }
  return equal;
}

MATCHER_P(Percent, rhs, "") {
  envoy::type::v3::FractionalPercent expected;
  expected.set_numerator(rhs);
  expected.set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
  return TestUtility::protoEqual(expected, arg, /*ignore_repeated_field_ordering=*/false);
}

MATCHER_P(JsonStringEq, expected, "") {
  const bool equal = TestUtility::jsonStringEqual(arg, expected);
  if (!equal) {
    *result_listener << "\n"
                     << TestUtility::addLeftAndRightPadding("Expected JSON string:") << "\n"
                     << expected
                     << TestUtility::addLeftAndRightPadding("is not equal to actual JSON string:")
                     << "\n"
                     << arg << TestUtility::addLeftAndRightPadding("") // line full of padding
                     << "\n";
  }
  return equal;
}

} // namespace Envoy
