#include "utility.h"

#ifdef WIN32
#include <windows.h>
// <windows.h> uses macros to #define a ton of symbols, two of which (DELETE and GetMessage)
// interfere with our code. DELETE shows up in the base.pb.h header generated from
// api/envoy/api/core/base.proto. Since it's a generated header, we can't #undef DELETE at
// the top of that header to avoid the collision. Similarly, GetMessage shows up in generated
// protobuf code so we can't #undef the symbol there.
#undef DELETE
#undef GetMessage
#endif

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/route/route.pb.h"
#include "envoy/buffer/buffer.h"
#include "envoy/http/codec.h"
#include "envoy/service/discovery/v2/rtds.pb.h"

#include "common/api/api_impl.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/common/lock_guard.h"
#include "common/common/stack_array.h"
#include "common/common/thread_impl.h"
#include "common/common/utility.h"
#include "common/config/resources.h"
#include "common/json/json_loader.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/filesystem/directory.h"
#include "common/filesystem/filesystem_impl.h"

#include "test/test_common/printers.h"
#include "test/test_common/test_time.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "test/mocks/stats/mocks.h"
#include "gtest/gtest.h"

using testing::GTEST_FLAG(random_seed);

namespace Envoy {

// The purpose of using the static seed here is to use --test_arg=--gtest_random_seed=[seed]
// to specify the seed of the problem to replay.
int32_t getSeed() {
  static const int32_t seed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
  return seed;
}

TestRandomGenerator::TestRandomGenerator()
    : seed_(GTEST_FLAG(random_seed) == 0 ? getSeed() : GTEST_FLAG(random_seed)), generator_(seed_) {
  std::cerr << "TestRandomGenerator running with seed " << seed_ << "\n";
}

uint64_t TestRandomGenerator::random() { return generator_(); }

bool TestUtility::headerMapEqualIgnoreOrder(const Http::HeaderMap& lhs,
                                            const Http::HeaderMap& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  struct State {
    const Http::HeaderMap& lhs;
    bool equal;
  };

  State state{lhs, true};
  rhs.iterate(
      [](const Http::HeaderEntry& header, void* context) -> Http::HeaderMap::Iterate {
        State* state = static_cast<State*>(context);
        const Http::HeaderEntry* entry =
            state->lhs.get(Http::LowerCaseString(std::string(header.key().getStringView())));
        if (entry == nullptr || (entry->value() != header.value().getStringView())) {
          state->equal = false;
          return Http::HeaderMap::Iterate::Break;
        }
        return Http::HeaderMap::Iterate::Continue;
      },
      &state);

  return state.equal;
}

bool TestUtility::buffersEqual(const Buffer::Instance& lhs, const Buffer::Instance& rhs) {
  if (lhs.length() != rhs.length()) {
    return false;
  }

  // Check whether the two buffers contain the same content. It is valid for the content
  // to be arranged differently in the buffers. For example, lhs could have one slice
  // containing 10 bytes while rhs has ten slices containing one byte each.
  uint64_t lhs_num_slices = lhs.getRawSlices(nullptr, 0);
  uint64_t rhs_num_slices = rhs.getRawSlices(nullptr, 0);
  STACK_ARRAY(lhs_slices, Buffer::RawSlice, lhs_num_slices);
  lhs.getRawSlices(lhs_slices.begin(), lhs_num_slices);
  STACK_ARRAY(rhs_slices, Buffer::RawSlice, rhs_num_slices);
  rhs.getRawSlices(rhs_slices.begin(), rhs_num_slices);
  size_t rhs_slice = 0;
  size_t rhs_offset = 0;
  for (size_t lhs_slice = 0; lhs_slice < lhs_num_slices; lhs_slice++) {
    for (size_t lhs_offset = 0; lhs_offset < lhs_slices[lhs_slice].len_; lhs_offset++) {
      while (rhs_offset >= rhs_slices[rhs_slice].len_) {
        rhs_slice++;
        ASSERT(rhs_slice < rhs_num_slices);
        rhs_offset = 0;
      }
      auto lhs_str = static_cast<const uint8_t*>(lhs_slices[lhs_slice].mem_);
      auto rhs_str = static_cast<const uint8_t*>(rhs_slices[rhs_slice].mem_);
      if (lhs_str[lhs_offset] != rhs_str[rhs_offset]) {
        return false;
      }
      rhs_offset++;
    }
  }

  return true;
}

void TestUtility::feedBufferWithRandomCharacters(Buffer::Instance& buffer, uint64_t n_char,
                                                 uint64_t seed) {
  const std::string sample = "Neque porro quisquam est qui dolorem ipsum..";
  std::mt19937 generate(seed);
  std::uniform_int_distribution<> distribute(1, sample.length() - 1);
  std::string str{};
  for (uint64_t n = 0; n < n_char; ++n) {
    str += sample.at(distribute(generate));
  }
  buffer.add(str);
}

Stats::CounterSharedPtr TestUtility::findCounter(Stats::Store& store, const std::string& name) {
  return findByName(store.counters(), name);
}

Stats::GaugeSharedPtr TestUtility::findGauge(Stats::Store& store, const std::string& name) {
  return findByName(store.gauges(), name);
}

void TestUtility::waitForCounterEq(Stats::Store& store, const std::string& name, uint64_t value,
                                   Event::TestTimeSystem& time_system) {
  while (findCounter(store, name) == nullptr || findCounter(store, name)->value() != value) {
    time_system.sleep(std::chrono::milliseconds(10));
  }
}

void TestUtility::waitForCounterGe(Stats::Store& store, const std::string& name, uint64_t value,
                                   Event::TestTimeSystem& time_system) {
  while (findCounter(store, name) == nullptr || findCounter(store, name)->value() < value) {
    time_system.sleep(std::chrono::milliseconds(10));
  }
}

void TestUtility::waitForGaugeGe(Stats::Store& store, const std::string& name, uint64_t value,
                                 Event::TestTimeSystem& time_system) {
  while (findGauge(store, name) == nullptr || findGauge(store, name)->value() < value) {
    time_system.sleep(std::chrono::milliseconds(10));
  }
}

void TestUtility::waitForGaugeEq(Stats::Store& store, const std::string& name, uint64_t value,
                                 Event::TestTimeSystem& time_system) {
  while (findGauge(store, name) == nullptr || findGauge(store, name)->value() != value) {
    time_system.sleep(std::chrono::milliseconds(10));
  }
}

std::list<Network::DnsResponse>
TestUtility::makeDnsResponse(const std::list<std::string>& addresses, std::chrono::seconds ttl) {
  std::list<Network::DnsResponse> ret;
  for (const auto& address : addresses) {

    ret.emplace_back(Network::DnsResponse(Network::Utility::parseInternetAddress(address), ttl));
  }
  return ret;
}

std::vector<std::string> TestUtility::listFiles(const std::string& path, bool recursive) {
  std::vector<std::string> file_names;
  Filesystem::Directory directory(path);
  for (const Filesystem::DirectoryEntry& entry : directory) {
    std::string file_name = fmt::format("{}/{}", path, entry.name_);
    if (entry.type_ == Filesystem::FileType::Directory) {
      if (recursive && entry.name_ != "." && entry.name_ != "..") {
        std::vector<std::string> more_file_names = listFiles(file_name, recursive);
        file_names.insert(file_names.end(), more_file_names.begin(), more_file_names.end());
      }
    } else { // regular file
      file_names.push_back(file_name);
    }
  }
  return file_names;
}

std::string TestUtility::xdsResourceName(const ProtobufWkt::Any& resource) {
  if (resource.type_url() == Config::TypeUrl::get().Listener) {
    return TestUtility::anyConvert<envoy::api::v2::Listener>(resource).name();
  }
  if (resource.type_url() == Config::TypeUrl::get().RouteConfiguration) {
    return TestUtility::anyConvert<envoy::api::v2::RouteConfiguration>(resource).name();
  }
  if (resource.type_url() == Config::TypeUrl::get().Cluster) {
    return TestUtility::anyConvert<envoy::api::v2::Cluster>(resource).name();
  }
  if (resource.type_url() == Config::TypeUrl::get().ClusterLoadAssignment) {
    return TestUtility::anyConvert<envoy::api::v2::ClusterLoadAssignment>(resource).cluster_name();
  }
  if (resource.type_url() == Config::TypeUrl::get().VirtualHost) {
    return TestUtility::anyConvert<envoy::api::v2::route::VirtualHost>(resource).name();
  }
  if (resource.type_url() == Config::TypeUrl::get().Runtime) {
    return TestUtility::anyConvert<envoy::service::discovery::v2::Runtime>(resource).name();
  }
  throw EnvoyException(
      fmt::format("xdsResourceName does not know about type URL {}", resource.type_url()));
}

std::string TestUtility::addLeftAndRightPadding(absl::string_view to_pad, int desired_length) {
  int line_fill_len = desired_length - to_pad.length();
  int first_half_len = line_fill_len / 2;
  int second_half_len = line_fill_len - first_half_len;
  return absl::StrCat(std::string(first_half_len, '='), to_pad, std::string(second_half_len, '='));
}

std::vector<std::string> TestUtility::split(const std::string& source, char split) {
  return TestUtility::split(source, std::string{split});
}

std::vector<std::string> TestUtility::split(const std::string& source, const std::string& split,
                                            bool keep_empty_string) {
  std::vector<std::string> ret;
  const auto tokens_sv = StringUtil::splitToken(source, split, keep_empty_string);
  std::transform(tokens_sv.begin(), tokens_sv.end(), std::back_inserter(ret),
                 [](absl::string_view sv) { return std::string(sv); });
  return ret;
}

void TestUtility::renameFile(const std::string& old_name, const std::string& new_name) {
#ifdef WIN32
  // use MoveFileEx, since ::rename will not overwrite an existing file. See
  // https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/rename-wrename?view=vs-2017
  const BOOL rc = ::MoveFileEx(old_name.c_str(), new_name.c_str(), MOVEFILE_REPLACE_EXISTING);
  ASSERT_NE(0, rc);
#else
  const int rc = ::rename(old_name.c_str(), new_name.c_str());
  ASSERT_EQ(0, rc);
#endif
};

void TestUtility::createDirectory(const std::string& name) {
#ifdef WIN32
  ::_mkdir(name.c_str());
#else
  ::mkdir(name.c_str(), S_IRWXU);
#endif
}

void TestUtility::createSymlink(const std::string& target, const std::string& link) {
#ifdef WIN32
  const DWORD attributes = ::GetFileAttributes(target.c_str());
  ASSERT_NE(attributes, INVALID_FILE_ATTRIBUTES);
  int flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
  if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
    flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
  }

  const BOOLEAN rc = ::CreateSymbolicLink(link.c_str(), target.c_str(), flags);
  ASSERT_NE(rc, 0);
#else
  const int rc = ::symlink(target.c_str(), link.c_str());
  ASSERT_EQ(rc, 0);
#endif
}

// static
absl::Time TestUtility::parseTime(const std::string& input, const std::string& input_format) {
  absl::Time time;
  std::string parse_error;
  EXPECT_TRUE(absl::ParseTime(input_format, input, &time, &parse_error))
      << " error \"" << parse_error << "\" from failing to parse timestamp \"" << input
      << "\" with format string \"" << input_format << "\"";
  return time;
}

// static
std::string TestUtility::formatTime(const absl::Time input, const std::string& output_format) {
  static const absl::TimeZone utc = absl::UTCTimeZone();
  return absl::FormatTime(output_format, input, utc);
}

// static
std::string TestUtility::formatTime(const SystemTime input, const std::string& output_format) {
  return TestUtility::formatTime(absl::FromChrono(input), output_format);
}

// static
std::string TestUtility::convertTime(const std::string& input, const std::string& input_format,
                                     const std::string& output_format) {
  return TestUtility::formatTime(TestUtility::parseTime(input, input_format), output_format);
}

// static
bool TestUtility::gaugesZeroed(const std::vector<Stats::GaugeSharedPtr>& gauges) {
  // Returns true if all gauges are 0 except the circuit_breaker remaining resource
  // gauges which default to the resource max.
  std::regex omitted(".*circuit_breakers\\..*\\.remaining.*");
  for (const Stats::GaugeSharedPtr& gauge : gauges) {
    if (!std::regex_match(gauge->name(), omitted) && gauge->value() != 0) {
      return false;
    }
  }
  return true;
}

void ConditionalInitializer::setReady() {
  Thread::LockGuard lock(mutex_);
  EXPECT_FALSE(ready_);
  ready_ = true;
  cv_.notifyAll();
}

void ConditionalInitializer::waitReady() {
  Thread::LockGuard lock(mutex_);
  if (ready_) {
    ready_ = false;
    return;
  }

  cv_.wait(mutex_);
  EXPECT_TRUE(ready_);
  ready_ = false;
}

void ConditionalInitializer::wait() {
  Thread::LockGuard lock(mutex_);
  while (!ready_) {
    cv_.wait(mutex_);
  }
}

AtomicFileUpdater::AtomicFileUpdater(const std::string& filename)
    : link_(filename), new_link_(absl::StrCat(filename, ".new")),
      target1_(absl::StrCat(filename, ".target1")), target2_(absl::StrCat(filename, ".target2")),
      use_target1_(true) {
  unlink(link_.c_str());
  unlink(new_link_.c_str());
  unlink(target1_.c_str());
  unlink(target2_.c_str());
}

void AtomicFileUpdater::update(const std::string& contents) {
  const std::string target = use_target1_ ? target1_ : target2_;
  use_target1_ = !use_target1_;
  {
    std::ofstream file(target);
    file << contents;
  }
  TestUtility::createSymlink(target, new_link_);
  TestUtility::renameFile(new_link_, link_);
}

constexpr std::chrono::milliseconds TestUtility::DefaultTimeout;

namespace Http {

// Satisfy linker
const uint32_t Http2Settings::DEFAULT_HPACK_TABLE_SIZE;
const uint32_t Http2Settings::DEFAULT_MAX_CONCURRENT_STREAMS;
const uint32_t Http2Settings::DEFAULT_INITIAL_STREAM_WINDOW_SIZE;
const uint32_t Http2Settings::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE;
const uint32_t Http2Settings::MIN_INITIAL_STREAM_WINDOW_SIZE;
const uint32_t Http2Settings::DEFAULT_MAX_OUTBOUND_FRAMES;
const uint32_t Http2Settings::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES;
const uint32_t Http2Settings::DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD;
const uint32_t Http2Settings::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM;
const uint32_t Http2Settings::DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT;

TestHeaderMapImpl::TestHeaderMapImpl() = default;

TestHeaderMapImpl::TestHeaderMapImpl(
    const std::initializer_list<std::pair<std::string, std::string>>& values) {
  for (auto& value : values) {
    addCopy(value.first, value.second);
  }
}

TestHeaderMapImpl::TestHeaderMapImpl(const HeaderMap& rhs) : HeaderMapImpl(rhs) {}

TestHeaderMapImpl::TestHeaderMapImpl(const TestHeaderMapImpl& rhs)
    : TestHeaderMapImpl(static_cast<const HeaderMap&>(rhs)) {}

TestHeaderMapImpl& TestHeaderMapImpl::operator=(const TestHeaderMapImpl& rhs) {
  if (&rhs == this) {
    return *this;
  }

  clear();
  copyFrom(rhs);

  return *this;
}

void TestHeaderMapImpl::addCopy(const std::string& key, const std::string& value) {
  addCopy(LowerCaseString(key), value);
}

void TestHeaderMapImpl::remove(const std::string& key) { remove(LowerCaseString(key)); }

std::string TestHeaderMapImpl::get_(const std::string& key) const {
  return get_(LowerCaseString(key));
}

std::string TestHeaderMapImpl::get_(const LowerCaseString& key) const {
  const HeaderEntry* header = get(key);
  if (!header) {
    return EMPTY_STRING;
  } else {
    return std::string(header->value().getStringView());
  }
}

bool TestHeaderMapImpl::has(const std::string& key) { return get(LowerCaseString(key)) != nullptr; }

bool TestHeaderMapImpl::has(const LowerCaseString& key) { return get(key) != nullptr; }

} // namespace Http

namespace Api {

class TestImplProvider {
protected:
  Event::GlobalTimeSystem global_time_system_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> default_stats_store_;
};

class TestImpl : public TestImplProvider, public Impl {
public:
  TestImpl(Thread::ThreadFactory& thread_factory, Stats::Store& stats_store,
           Filesystem::Instance& file_system)
      : Impl(thread_factory, stats_store, global_time_system_, file_system) {}
  TestImpl(Thread::ThreadFactory& thread_factory, Event::TimeSystem& time_system,
           Filesystem::Instance& file_system)
      : Impl(thread_factory, default_stats_store_, time_system, file_system) {}
  TestImpl(Thread::ThreadFactory& thread_factory, Filesystem::Instance& file_system)
      : Impl(thread_factory, default_stats_store_, global_time_system_, file_system) {}
};

ApiPtr createApiForTest() {
  return std::make_unique<TestImpl>(Thread::threadFactoryForTest(),
                                    Filesystem::fileSystemForTest());
}

ApiPtr createApiForTest(Stats::Store& stat_store) {
  return std::make_unique<TestImpl>(Thread::threadFactoryForTest(), stat_store,
                                    Filesystem::fileSystemForTest());
}

ApiPtr createApiForTest(Event::TimeSystem& time_system) {
  return std::make_unique<TestImpl>(Thread::threadFactoryForTest(), time_system,
                                    Filesystem::fileSystemForTest());
}

ApiPtr createApiForTest(Stats::Store& stat_store, Event::TimeSystem& time_system) {
  return std::make_unique<Impl>(Thread::threadFactoryForTest(), stat_store, time_system,
                                Filesystem::fileSystemForTest());
}

} // namespace Api
} // namespace Envoy
