#pragma once

#include <stdlib.h>

#include <list>
#include <random>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/network/address.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/store.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/c_smart_ptr.h"
#include "common/common/thread.h"
#include "common/http/header_map_impl.h"
#include "common/protobuf/utility.h"
#include "common/stats/raw_stat_data.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AssertionFailure;
using testing::AssertionResult;
using testing::AssertionSuccess;
using testing::Invoke;

namespace Envoy {
#define EXPECT_THROW_WITH_MESSAGE(statement, expected_exception, message)                          \
  try {                                                                                            \
    statement;                                                                                     \
    ADD_FAILURE() << "Exception should take place. It did not.";                                   \
  } catch (expected_exception & e) {                                                               \
    EXPECT_EQ(message, std::string(e.what()));                                                     \
  }

#define EXPECT_THROW_WITH_REGEX(statement, expected_exception, regex_str)                          \
  try {                                                                                            \
    statement;                                                                                     \
    ADD_FAILURE() << "Exception should take place. It did not.";                                   \
  } catch (expected_exception & e) {                                                               \
    EXPECT_THAT(e.what(), ::testing::ContainsRegex(regex_str));                                    \
  }

#define EXPECT_THROW_WITHOUT_REGEX(statement, expected_exception, regex_str)                       \
  try {                                                                                            \
    statement;                                                                                     \
    ADD_FAILURE() << "Exception should take place. It did not.";                                   \
  } catch (expected_exception & e) {                                                               \
    EXPECT_THAT(e.what(), ::testing::Not(::testing::ContainsRegex(regex_str)));                    \
  }

#define VERBOSE_EXPECT_NO_THROW(statement)                                                         \
  try {                                                                                            \
    statement;                                                                                     \
  } catch (EnvoyException & e) {                                                                   \
    ADD_FAILURE() << "Unexpected exception: " << std::string(e.what());                            \
  }

/*
  Macro to use instead of EXPECT_DEATH when stderr is produced by a logger.
  It temporarily installs stderr sink and restores the original logger sink after the test
  completes and sdterr_sink object goes of of scope.
  EXPECT_DEATH(statement, regex) test passes when statement causes crash and produces error message
  matching regex. Test fails when statement does not crash or it crashes but message does not
  match regex. If a message produced during crash is redirected away from strerr, the test fails.
  By installing StderrSinkDelegate, the macro forces EXPECT_DEATH to send any output produced by
  statement to stderr.
*/
#define EXPECT_DEATH_LOG_TO_STDERR(statement, message)                                             \
  do {                                                                                             \
    Logger::StderrSinkDelegate stderr_sink(Logger::Registry::getSink());                           \
    EXPECT_DEATH(statement, message);                                                              \
  } while (false)

#define VERIFY_ASSERTION(statement)                                                                \
  do {                                                                                             \
    ::testing::AssertionResult status = statement;                                                 \
    if (!status) {                                                                                 \
      return status;                                                                               \
    }                                                                                              \
  } while (false)

// Random number generator which logs its seed to stderr. To repeat a test run with a non-zero seed
// one can run the test with --test_arg=--gtest_random_seed=[seed]
class TestRandomGenerator {
public:
  TestRandomGenerator();

  uint64_t random();

private:
  const int32_t seed_;
  std::ranlux48 generator_;
};

class TestUtility {
public:
  /**
   * Compare 2 HeaderMaps.
   * @param lhs supplies HeaderMaps 1.
   * @param rhs supplies HeaderMaps 2.
   * @return TRUE if the HeaderMapss are equal, ignoring the order of the
   * headers, false if not.
   */
  static bool headerMapEqualIgnoreOrder(const Http::HeaderMap& lhs, const Http::HeaderMap& rhs);

  /**
   * Compare 2 buffers.
   * @param lhs supplies buffer 1.
   * @param rhs supplies buffer 2.
   * @return TRUE if the buffers are equal, false if not.
   */
  static bool buffersEqual(const Buffer::Instance& lhs, const Buffer::Instance& rhs);

  /**
   * Feed a buffer with random characters.
   * @param buffer supplies the buffer to be fed.
   * @param n_char number of characters that should be added to the supplied buffer.
   * @param seed seeds pseudo-random number genarator (default = 0).
   */
  static void feedBufferWithRandomCharacters(Buffer::Instance& buffer, uint64_t n_char,
                                             uint64_t seed = 0);

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
   * Convert a string list of IP addresses into a list of network addresses usable for DNS
   * response testing.
   */
  static std::list<Network::Address::InstanceConstSharedPtr>
  makeDnsResponse(const std::list<std::string>& addresses);

  /**
   * List files in a given directory path
   *
   * @param path directory path to list
   * @param recursive whether or not to traverse subdirectories
   * @return std::vector<std::string> filenames
   */
  static std::vector<std::string> listFiles(const std::string& path, bool recursive);

  /**
   * Compare two protos of the same type for equality.
   *
   * @param lhs proto on LHS.
   * @param rhs proto on RHS.
   * @return bool indicating whether the protos are equal.
   */
  static bool protoEqual(const Protobuf::Message& lhs, const Protobuf::Message& rhs) {
    return Protobuf::util::MessageDifferencer::Equivalent(lhs, rhs);
  }

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
   * @return bool indicating whether the RepeatedPtrField are equal. TestUtility::protoEqual() is
   *              used for individual element testing.
   */
  template <class ProtoType>
  static bool repeatedPtrFieldEqual(const Protobuf::RepeatedPtrField<ProtoType>& lhs,
                                    const Protobuf::RepeatedPtrField<ProtoType>& rhs) {
    if (lhs.size() != rhs.size()) {
      return false;
    }

    for (int i = 0; i < lhs.size(); ++i) {
      if (!TestUtility::protoEqual(lhs[i], rhs[i])) {
        return false;
      }
    }

    return true;
  }

  template <class ProtoType>
  static AssertionResult
  assertRepeatedPtrFieldEqual(const Protobuf::RepeatedPtrField<ProtoType>& lhs,
                              const Protobuf::RepeatedPtrField<ProtoType>& rhs) {
    if (!repeatedPtrFieldEqual(lhs, rhs)) {
      return AssertionFailure() << RepeatedPtrUtil::debugString(lhs) << " does not match "
                                << RepeatedPtrUtil::debugString(rhs);
    }

    return AssertionSuccess();
  }

  /**
   * Parse bootstrap config from v1 JSON static config string.
   * @param json_string source v1 JSON static config string.
   * @return envoy::config::bootstrap::v2::Bootstrap.
   */
  static envoy::config::bootstrap::v2::Bootstrap
  parseBootstrapFromJson(const std::string& json_string);

  /**
   * Returns a "novel" IPv4 loopback address, if available.
   * For many tests, we want a loopback address other than 127.0.0.1 where possible. For some
   * platforms such as OSX, only 127.0.0.1 is available for IPv4 loopback.
   *
   * @return string 127.0.0.x , where x is "1" for OSX and "9" otherwise.
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
    MessageUtil::loadFromYaml(yaml, message);
    return message;
  }

  // Allows pretty printed test names for TEST_P using TestEnvironment::getIpVersionsForTest().
  //
  // Tests using this will be of the form IpVersions/SslSocketTest.HalfClose/IPv4
  // instead of IpVersions/SslSocketTest.HalfClose/1
  static std::string
  ipTestParamsToString(const testing::TestParamInfo<Network::Address::IpVersion>& params) {
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

  static constexpr std::chrono::milliseconds DefaultTimeout = std::chrono::milliseconds(10000);
};

/**
 * This utility class wraps the common case of having a cross-thread "one shot" ready condition.
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

private:
  Thread::CondVar cv_;
  Thread::MutexBasicLockable mutex_;
  bool ready_{false};
};

class ScopedFdCloser {
public:
  ScopedFdCloser(int fd);
  ~ScopedFdCloser();

private:
  int fd_;
};

namespace Http {

/**
 * A test version of HeaderMapImpl that adds some niceties around letting us use
 * std::string instead of always doing LowerCaseString() by hand.
 */
class TestHeaderMapImpl : public HeaderMapImpl {
public:
  TestHeaderMapImpl();
  TestHeaderMapImpl(const std::initializer_list<std::pair<std::string, std::string>>& values);
  TestHeaderMapImpl(const HeaderMap& rhs);

  // The above constructor for TestHeaderMap is not an actual copy constructor.
  TestHeaderMapImpl(const TestHeaderMapImpl& rhs);
  TestHeaderMapImpl& operator=(const TestHeaderMapImpl& rhs);

  bool operator==(const TestHeaderMapImpl& rhs) const { return HeaderMapImpl::operator==(rhs); }

  friend std::ostream& operator<<(std::ostream& os, const TestHeaderMapImpl& p) {
    p.iterate(
        [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
          std::ostream* local_os = static_cast<std::ostream*>(context);
          *local_os << header.key().c_str() << " " << header.value().c_str() << std::endl;
          return HeaderMap::Iterate::Continue;
        },
        &os);
    return os;
  }

  using HeaderMapImpl::addCopy;
  using HeaderMapImpl::remove;
  void addCopy(const std::string& key, const std::string& value);
  void remove(const std::string& key);
  std::string get_(const std::string& key);
  std::string get_(const LowerCaseString& key);
  bool has(const std::string& key);
  bool has(const LowerCaseString& key);
};

} // namespace Http

namespace Stats {

/**
 * This is a heap test allocator that works similar to how the shared memory allocator works in
 * terms of reference counting, etc.
 */
class TestAllocator : public RawStatDataAllocator {
public:
  TestAllocator(const StatsOptions& stats_options) : stats_options_(stats_options) {}
  ~TestAllocator() { EXPECT_TRUE(stats_.empty()); }

  RawStatData* alloc(absl::string_view name) override {
    CSmartPtr<RawStatData, freeAdapter>& stat_ref = stats_[std::string(name)];
    if (!stat_ref) {
      stat_ref.reset(static_cast<RawStatData*>(
          ::calloc(RawStatData::structSizeWithOptions(stats_options_), 1)));
      stat_ref->initialize(name, stats_options_);
    } else {
      stat_ref->ref_count_++;
    }

    return stat_ref.get();
  }

  void free(RawStatData& data) override {
    if (--data.ref_count_ > 0) {
      return;
    }

    if (stats_.erase(std::string(data.name_)) == 0) {
      FAIL();
    }
  }

private:
  static void freeAdapter(RawStatData* data) { ::free(data); }
  std::unordered_map<std::string, CSmartPtr<RawStatData, freeAdapter>> stats_;
  const StatsOptions& stats_options_;
};

class MockedTestAllocator : public RawStatDataAllocator {
public:
  MockedTestAllocator(const StatsOptions& stats_options);
  virtual ~MockedTestAllocator();

  MOCK_METHOD1(alloc, RawStatData*(absl::string_view name));
  MOCK_METHOD1(free, void(RawStatData& data));

  TestAllocator alloc_;
};

} // namespace Stats

MATCHER_P(HeaderMapEqualIgnoreOrder, rhs, "") {
  *result_listener << *rhs << " is not equal to " << *arg;
  return TestUtility::headerMapEqualIgnoreOrder(*arg, *rhs);
}

MATCHER_P(ProtoEq, rhs, "") { return TestUtility::protoEqual(arg, rhs); }

MATCHER_P(RepeatedProtoEq, rhs, "") { return TestUtility::repeatedPtrFieldEqual(arg, rhs); }

} // namespace Envoy
