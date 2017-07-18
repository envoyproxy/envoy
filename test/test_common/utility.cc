#include "utility.h"

#include <dirent.h>
#include <unistd.h>

#include <cstdint>
#include <iostream>
#include <list>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codec.h"

#include "common/common/empty_string.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "test/test_common/printers.h"

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

using testing::GTEST_FLAG(random_seed);

namespace Envoy {

static const int32_t SEED = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();

TestRandomGenerator::TestRandomGenerator()
    : generator_(GTEST_FLAG(random_seed) == 0 ? SEED : GTEST_FLAG(random_seed)) {
  const int32_t seed = GTEST_FLAG(random_seed) == 0 ? SEED : GTEST_FLAG(random_seed);
  std::cerr << "TestRandomGenerator running with seed " << seed;
}

uint64_t TestRandomGenerator::random() { return generator_(); }

bool TestUtility::buffersEqual(const Buffer::Instance& lhs, const Buffer::Instance& rhs) {
  if (lhs.length() != rhs.length()) {
    return false;
  }

  uint64_t lhs_num_slices = lhs.getRawSlices(nullptr, 0);
  uint64_t rhs_num_slices = rhs.getRawSlices(nullptr, 0);
  if (lhs_num_slices != rhs_num_slices) {
    return false;
  }

  Buffer::RawSlice lhs_slices[lhs_num_slices];
  lhs.getRawSlices(lhs_slices, lhs_num_slices);
  Buffer::RawSlice rhs_slices[rhs_num_slices];
  rhs.getRawSlices(rhs_slices, rhs_num_slices);
  for (size_t i = 0; i < lhs_num_slices; i++) {
    if (lhs_slices[i].len_ != rhs_slices[i].len_) {
      return false;
    }

    if (0 != memcmp(lhs_slices[i].mem_, rhs_slices[i].mem_, lhs_slices[i].len_)) {
      return false;
    }
  }

  return true;
}

std::string TestUtility::bufferToString(const Buffer::Instance& buffer) {
  std::string output;
  uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  buffer.getRawSlices(slices, num_slices);
  for (Buffer::RawSlice& slice : slices) {
    output.append(static_cast<const char*>(slice.mem_), slice.len_);
  }

  return output;
}

Stats::CounterSharedPtr TestUtility::findCounter(Stats::Store& store, const std::string& name) {
  for (auto counter : store.counters()) {
    if (counter->name() == name) {
      return counter;
    }
  }
  return nullptr;
}

Stats::GaugeSharedPtr TestUtility::findGauge(Stats::Store& store, const std::string& name) {
  for (auto gauge : store.gauges()) {
    if (gauge->name() == name) {
      return gauge;
    }
  }
  return nullptr;
}

std::list<Network::Address::InstanceConstSharedPtr>
TestUtility::makeDnsResponse(const std::list<std::string>& addresses) {
  std::list<Network::Address::InstanceConstSharedPtr> ret;
  for (const auto& address : addresses) {
    ret.emplace_back(Network::Utility::parseInternetAddress(address));
  }
  return ret;
}

std::vector<std::string> TestUtility::listFiles(const std::string& path, bool recursive) {
  DIR* dir = opendir(path.c_str());
  if (!dir) {
    throw std::runtime_error(fmt::format("Directory not found '{}'", path));
  }

  std::vector<std::string> file_names;
  dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    std::string file_name = fmt::format("{}/{}", path, std::string(entry->d_name));
    if (recursive && entry->d_type == DT_DIR && std::string(entry->d_name) != "." &&
        std::string(entry->d_name) != "..") {
      std::vector<std::string> more_file_names = listFiles(file_name, recursive);
      file_names.insert(file_names.end(), more_file_names.begin(), more_file_names.end());
      continue;
    } else if (entry->d_type == DT_DIR) {
      continue;
    }

    file_names.push_back(file_name);
  }

  closedir(dir);
  return file_names;
}

void ConditionalInitializer::setReady() {
  std::unique_lock<std::mutex> lock(mutex_);
  EXPECT_FALSE(ready_);
  ready_ = true;
  cv_.notify_all();
}

void ConditionalInitializer::waitReady() {
  std::unique_lock<std::mutex> lock(mutex_);
  if (ready_) {
    ready_ = false;
    return;
  }

  cv_.wait(lock);
  EXPECT_TRUE(ready_);
  ready_ = false;
}

ScopedFdCloser::ScopedFdCloser(int fd) : fd_(fd) {}
ScopedFdCloser::~ScopedFdCloser() { ::close(fd_); }

namespace Http {

// Satisfy linker
const uint32_t Http2Settings::DEFAULT_HPACK_TABLE_SIZE;
const uint32_t Http2Settings::DEFAULT_MAX_CONCURRENT_STREAMS;
const uint32_t Http2Settings::DEFAULT_INITIAL_STREAM_WINDOW_SIZE;
const uint32_t Http2Settings::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE;

TestHeaderMapImpl::TestHeaderMapImpl() : HeaderMapImpl() {}

TestHeaderMapImpl::TestHeaderMapImpl(
    const std::initializer_list<std::pair<std::string, std::string>>& values)
    : HeaderMapImpl() {
  for (auto& value : values) {
    addViaCopy(value.first, value.second);
  }
}

TestHeaderMapImpl::TestHeaderMapImpl(const HeaderMap& rhs) : HeaderMapImpl(rhs) {}

void TestHeaderMapImpl::addViaCopy(const std::string& key, const std::string& value) {
  addViaCopy(LowerCaseString(key), value);
}

void TestHeaderMapImpl::addViaCopy(const LowerCaseString& key, const std::string& value) {
  HeaderString key_string;
  key_string.setCopy(key.get().c_str(), key.get().size());
  HeaderString value_string;
  value_string.setCopy(value.c_str(), value.size());
  addViaMove(std::move(key_string), std::move(value_string));
}

std::string TestHeaderMapImpl::get_(const std::string& key) { return get_(LowerCaseString(key)); }

std::string TestHeaderMapImpl::get_(const LowerCaseString& key) {
  const HeaderEntry* header = get(key);
  if (!header) {
    return EMPTY_STRING;
  } else {
    return header->value().c_str();
  }
}

bool TestHeaderMapImpl::has(const std::string& key) { return get(LowerCaseString(key)) != nullptr; }

bool TestHeaderMapImpl::has(const LowerCaseString& key) { return get(key) != nullptr; }

} // namespace Http
} // namespace Envoy
