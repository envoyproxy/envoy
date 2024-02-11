#include "utility.h"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/platform.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/http/codec.h"
#include "envoy/server/overload/thread_local_overload_state.h"
#include "envoy/service/runtime/v3/rtds.pb.h"

#include "source/common/api/api_impl.h"
#include "source/common/common/fmt.h"
#include "source/common/common/lock_guard.h"
#include "source/common/common/thread_impl.h"
#include "source/common/common/utility.h"
#include "source/common/filesystem/directory.h"
#include "source/common/filesystem/filesystem_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/json/json_loader.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"

#include "test/mocks/common.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/resources.h"
#include "test/test_common/test_time.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

namespace Envoy {

bool TestUtility::headerMapEqualIgnoreOrder(const Http::HeaderMap& lhs,
                                            const Http::HeaderMap& rhs) {
  absl::flat_hash_set<std::string> lhs_keys;
  absl::flat_hash_set<std::string> rhs_keys;
  lhs.iterate([&lhs_keys](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    const std::string key{header.key().getStringView()};
    lhs_keys.insert(key);
    return Http::HeaderMap::Iterate::Continue;
  });
  bool values_match = true;
  rhs.iterate([&values_match, &lhs, &rhs,
               &rhs_keys](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    const std::string key{header.key().getStringView()};
    // Compare with canonicalized multi-value headers. This ensures we respect order within
    // a header.
    const auto lhs_entry =
        Http::HeaderUtility::getAllOfHeaderAsString(lhs, Http::LowerCaseString(key));
    const auto rhs_entry =
        Http::HeaderUtility::getAllOfHeaderAsString(rhs, Http::LowerCaseString(key));
    ASSERT(rhs_entry.result());
    if (lhs_entry.result() != rhs_entry.result()) {
      values_match = false;
      return Http::HeaderMap::Iterate::Break;
    }
    rhs_keys.insert(key);
    return Http::HeaderMap::Iterate::Continue;
  });
  return values_match && lhs_keys.size() == rhs_keys.size();
}

bool TestUtility::buffersEqual(const Buffer::Instance& lhs, const Buffer::Instance& rhs) {
  if (lhs.length() != rhs.length()) {
    return false;
  }

  // Check whether the two buffers contain the same content. It is valid for the content
  // to be arranged differently in the buffers. For example, lhs could have one slice
  // containing 10 bytes while rhs has ten slices containing one byte each.
  Buffer::RawSliceVector lhs_slices = lhs.getRawSlices();
  Buffer::RawSliceVector rhs_slices = rhs.getRawSlices();

  size_t rhs_slice = 0;
  size_t rhs_offset = 0;
  for (auto& lhs_slice : lhs_slices) {
    for (size_t lhs_offset = 0; lhs_offset < lhs_slice.len_; lhs_offset++) {
      while (rhs_offset >= rhs_slices[rhs_slice].len_) {
        rhs_slice++;
        ASSERT(rhs_slice < rhs_slices.size());
        rhs_offset = 0;
      }
      auto lhs_str = static_cast<const uint8_t*>(lhs_slice.mem_);
      auto rhs_str = static_cast<const uint8_t*>(rhs_slices[rhs_slice].mem_);
      if (lhs_str[lhs_offset] != rhs_str[rhs_offset]) {
        return false;
      }
      rhs_offset++;
    }
  }

  return true;
}

bool TestUtility::rawSlicesEqual(const Buffer::RawSlice* lhs, const Buffer::RawSlice* rhs,
                                 size_t num_slices) {
  for (size_t slice = 0; slice < num_slices; slice++) {
    auto rhs_slice = rhs[slice];
    auto lhs_slice = lhs[slice];
    if (rhs_slice.len_ != lhs_slice.len_) {
      return false;
    }
    auto rhs_slice_data = static_cast<const uint8_t*>(rhs_slice.mem_);
    auto lhs_slice_data = static_cast<const uint8_t*>(lhs_slice.mem_);
    for (size_t offset = 0; offset < rhs_slice.len_; offset++) {
      if (rhs_slice_data[offset] != lhs_slice_data[offset]) {
        return false;
      }
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

Stats::TextReadoutSharedPtr TestUtility::findTextReadout(Stats::Store& store,
                                                         const std::string& name) {
  return findByName(store.textReadouts(), name);
}

Stats::ParentHistogramSharedPtr TestUtility::findHistogram(Stats::Store& store,
                                                           const std::string& name) {
  return findByName(store.histograms(), name);
}

AssertionResult TestUtility::waitForCounterEq(Stats::Store& store, const std::string& name,
                                              uint64_t value, Event::TestTimeSystem& time_system,
                                              std::chrono::milliseconds timeout,
                                              Event::Dispatcher* dispatcher) {
  Event::TestTimeSystem::RealTimeBound bound(timeout);
  while (findCounter(store, name) == nullptr || findCounter(store, name)->value() != value) {
    time_system.advanceTimeWait(std::chrono::milliseconds(10));
    if (timeout != std::chrono::milliseconds::zero() && !bound.withinBound()) {
      std::string current_value;
      if (findCounter(store, name)) {
        current_value = absl::StrCat(findCounter(store, name)->value());
      } else {
        current_value = "nil";
      }
      return AssertionFailure() << fmt::format(
                 "timed out waiting for {} to be {}, current value {}", name, value, current_value);
    }
    if (dispatcher != nullptr) {
      dispatcher->run(Event::Dispatcher::RunType::NonBlock);
    }
  }
  return AssertionSuccess();
}

AssertionResult TestUtility::waitForCounterGe(Stats::Store& store, const std::string& name,
                                              uint64_t value, Event::TestTimeSystem& time_system,
                                              std::chrono::milliseconds timeout) {
  Event::TestTimeSystem::RealTimeBound bound(timeout);
  while (findCounter(store, name) == nullptr || findCounter(store, name)->value() < value) {
    time_system.advanceTimeWait(std::chrono::milliseconds(10));
    if (timeout != std::chrono::milliseconds::zero() && !bound.withinBound()) {
      return AssertionFailure() << fmt::format("timed out waiting for {} to be >= {}", name, value);
    }
  }
  return AssertionSuccess();
}

AssertionResult TestUtility::waitForGaugeGe(Stats::Store& store, const std::string& name,
                                            uint64_t value, Event::TestTimeSystem& time_system,
                                            std::chrono::milliseconds timeout) {
  Event::TestTimeSystem::RealTimeBound bound(timeout);
  while (findGauge(store, name) == nullptr || findGauge(store, name)->value() < value) {
    time_system.advanceTimeWait(std::chrono::milliseconds(10));
    if (timeout != std::chrono::milliseconds::zero() && !bound.withinBound()) {
      return AssertionFailure() << fmt::format("timed out waiting for {} to be {}", name, value);
    }
  }
  return AssertionSuccess();
}

AssertionResult TestUtility::waitForGaugeEq(Stats::Store& store, const std::string& name,
                                            uint64_t value, Event::TestTimeSystem& time_system,
                                            std::chrono::milliseconds timeout) {
  Event::TestTimeSystem::RealTimeBound bound(timeout);
  while (findGauge(store, name) == nullptr || findGauge(store, name)->value() != value) {
    time_system.advanceTimeWait(std::chrono::milliseconds(10));
    if (timeout != std::chrono::milliseconds::zero() && !bound.withinBound()) {
      std::string current_value;
      if (findGauge(store, name)) {
        current_value = absl::StrCat(findGauge(store, name)->value());
      } else {
        current_value = "nil";
      }
      return AssertionFailure() << fmt::format(
                 "timed out waiting for {} to be {}, current value {}", name, value, current_value);
    }
  }
  return AssertionSuccess();
}

AssertionResult TestUtility::waitForProactiveOverloadResourceUsageEq(
    Server::ThreadLocalOverloadState& overload_state,
    const Server::OverloadProactiveResourceName resource_name, int64_t expected_value,
    Event::TestTimeSystem& time_system, Event::Dispatcher& dispatcher,
    std::chrono::milliseconds timeout) {
  Event::TestTimeSystem::RealTimeBound bound(timeout);
  const auto& monitor = overload_state.getProactiveResourceMonitorForTest(resource_name);
  while (monitor->currentResourceUsage() != expected_value) {
    time_system.advanceTimeWait(std::chrono::milliseconds(10));
    if (timeout != std::chrono::milliseconds::zero() && !bound.withinBound()) {
      uint64_t current_value;
      current_value = monitor->currentResourceUsage();
      return AssertionFailure() << fmt::format(
                 "timed out waiting for proactive resource to be {}, current value {}",
                 expected_value, current_value);
    }
    dispatcher.run(Event::Dispatcher::RunType::NonBlock);
  }
  return AssertionSuccess();
}

AssertionResult TestUtility::waitForGaugeDestroyed(Stats::Store& store, const std::string& name,
                                                   Event::TestTimeSystem& time_system) {
  while (findGauge(store, name) != nullptr) {
    time_system.advanceTimeWait(std::chrono::milliseconds(10));
  }
  return AssertionSuccess();
}

AssertionResult TestUtility::waitForNumHistogramSamplesGe(Stats::Store& store,
                                                          const std::string& name,
                                                          uint64_t min_sample_count_required,
                                                          Event::TestTimeSystem& time_system,
                                                          Event::Dispatcher& main_dispatcher,
                                                          std::chrono::milliseconds timeout) {
  Event::TestTimeSystem::RealTimeBound bound(timeout);
  while (true) {
    auto histo = findByName<Stats::ParentHistogramSharedPtr>(store.histograms(), name);
    if (histo) {
      uint64_t sample_count = readSampleCount(main_dispatcher, *histo);
      if (sample_count >= min_sample_count_required) {
        break;
      }
    }

    time_system.advanceTimeWait(std::chrono::milliseconds(10));

    if (timeout != std::chrono::milliseconds::zero() && !bound.withinBound()) {
      return AssertionFailure() << fmt::format("timed out waiting for {} to have {} samples", name,
                                               min_sample_count_required);
    }
  }
  return AssertionSuccess();
}

AssertionResult TestUtility::waitUntilHistogramHasSamples(Stats::Store& store,
                                                          const std::string& name,
                                                          Event::TestTimeSystem& time_system,
                                                          Event::Dispatcher& main_dispatcher,
                                                          std::chrono::milliseconds timeout) {
  return waitForNumHistogramSamplesGe(store, name, 1, time_system, main_dispatcher, timeout);
}

uint64_t TestUtility::readSampleCount(Event::Dispatcher& main_dispatcher,
                                      const Stats::ParentHistogram& histogram) {
  // Note: we need to read the sample count from the main thread, to avoid data races.
  uint64_t sample_count = 0;
  absl::Notification notification;

  main_dispatcher.post([&] {
    sample_count = histogram.cumulativeStatistics().sampleCount();
    notification.Notify();
  });
  notification.WaitForNotification();

  return sample_count;
}

double TestUtility::readSampleSum(Event::Dispatcher& main_dispatcher,
                                  const Stats::ParentHistogram& histogram) {
  // Note: we need to read the sample count from the main thread, to avoid data races.
  double sample_sum = 0;
  absl::Notification notification;

  main_dispatcher.post([&] {
    sample_sum = histogram.cumulativeStatistics().sampleSum();
    notification.Notify();
  });
  notification.WaitForNotification();

  return sample_sum;
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

std::string TestUtility::uniqueFilename(absl::string_view prefix) {
  return absl::StrCat(prefix, "_", getpid(), "_",
                      std::chrono::system_clock::now().time_since_epoch().count());
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
std::string TestUtility::nonZeroedGauges(const std::vector<Stats::GaugeSharedPtr>& gauges) {
  // Returns all gauges that are 0 except the circuit_breaker remaining resource
  // gauges which default to the resource max.
  std::regex omitted(".*circuit_breakers\\..*\\.remaining.*");
  std::string non_zero;
  for (const Stats::GaugeSharedPtr& gauge : gauges) {
    if (!std::regex_match(gauge->name(), omitted) && gauge->value() != 0) {
      non_zero.append(fmt::format("{}: {}; ", gauge->name(), gauge->value()));
    }
  }
  return non_zero;
}

// static
bool TestUtility::gaugesZeroed(const std::vector<Stats::GaugeSharedPtr>& gauges) {
  return nonZeroedGauges(gauges).empty();
}

// static
bool TestUtility::gaugesZeroed(
    const std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>>& gauges) {
  // Returns true if all gauges are 0 except the circuit_breaker remaining resource
  // gauges which default to the resource max.
  std::regex omitted(".*circuit_breakers\\..*\\.remaining.*");
  for (const auto& gauge : gauges) {
    if (!std::regex_match(std::string(gauge.first), omitted) && gauge.second.get().value() != 0) {
      return false;
    }
  }
  return true;
}

void ConditionalInitializer::setReady() {
  absl::MutexLock lock(&mutex_);
  EXPECT_FALSE(ready_);
  ready_ = true;
}

void ConditionalInitializer::waitReady() {
  absl::MutexLock lock(&mutex_);
  if (ready_) {
    ready_ = false;
    return;
  }

  mutex_.Await(absl::Condition(&ready_));
  EXPECT_TRUE(ready_);
  ready_ = false;
}

void ConditionalInitializer::wait() {
  absl::MutexLock lock(&mutex_);
  mutex_.Await(absl::Condition(&ready_));
  EXPECT_TRUE(ready_);
}

namespace Api {

class TestImplProvider {
protected:
  Event::GlobalTimeSystem global_time_system_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> default_stats_store_;
  testing::NiceMock<Random::MockRandomGenerator> mock_random_generator_;
  envoy::config::bootstrap::v3::Bootstrap empty_bootstrap_;
};

class TestImpl : public TestImplProvider, public Impl {
public:
  TestImpl(Thread::ThreadFactory& thread_factory, Filesystem::Instance& file_system,
           Stats::Store* stats_store = nullptr, Event::TimeSystem* time_system = nullptr,
           Random::RandomGenerator* random = nullptr)
      : Impl(thread_factory, stats_store ? *stats_store : default_stats_store_,
             time_system ? *time_system : global_time_system_, file_system,
             random ? *random : mock_random_generator_, empty_bootstrap_) {}
};

ApiPtr createApiForTest() {
  return std::make_unique<TestImpl>(Thread::threadFactoryForTest(),
                                    Filesystem::fileSystemForTest());
}

ApiPtr createApiForTest(Filesystem::Instance& filesystem) {
  return std::make_unique<TestImpl>(Thread::threadFactoryForTest(), filesystem);
}

ApiPtr createApiForTest(Random::RandomGenerator& random) {
  return std::make_unique<TestImpl>(Thread::threadFactoryForTest(), Filesystem::fileSystemForTest(),
                                    nullptr, nullptr, &random);
}

ApiPtr createApiForTest(Stats::Store& stat_store) {
  return std::make_unique<TestImpl>(Thread::threadFactoryForTest(), Filesystem::fileSystemForTest(),
                                    &stat_store);
}

ApiPtr createApiForTest(Stats::Store& stat_store, Random::RandomGenerator& random) {
  return std::make_unique<TestImpl>(Thread::threadFactoryForTest(), Filesystem::fileSystemForTest(),
                                    &stat_store, nullptr, &random);
}

ApiPtr createApiForTest(Event::TimeSystem& time_system) {
  return std::make_unique<TestImpl>(Thread::threadFactoryForTest(), Filesystem::fileSystemForTest(),
                                    nullptr, &time_system);
}

ApiPtr createApiForTest(Stats::Store& stat_store, Event::TimeSystem& time_system) {
  return std::make_unique<TestImpl>(Thread::threadFactoryForTest(), Filesystem::fileSystemForTest(),
                                    &stat_store, &time_system);
}

} // namespace Api
} // namespace Envoy
