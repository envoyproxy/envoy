#include <memory>
#include <string>

#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/extensions/key_value/platform/config.h"
#include "library/common/extensions/key_value/platform/platform.pb.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace KeyValue {
namespace {

class TestPlatformInterface : public PlatformInterface {
  virtual void save(const std::string& key, const std::string& contents) {
    store_.erase(key);
    store_.emplace(key, contents);
  }
  virtual std::string read(const std::string& key) const {
    auto it = store_.find(key);
    if (it == store_.end()) {
      return "";
    }
    return it->second;
  }

  absl::flat_hash_map<std::string, std::string> store_;
};

class PlatformStoreTest : public testing::Test {
protected:
  PlatformStoreTest() { createStore(); }

  void createStore() {
    flush_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
    store_ = std::make_unique<PlatformKeyValueStore>(dispatcher_, save_interval_, mock_platform_,
                                                     std::numeric_limits<uint64_t>::max(), key_);
  }
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::string key_{"key"};
  std::unique_ptr<PlatformKeyValueStore> store_{};
  std::chrono::seconds save_interval_{5};
  Event::MockTimer* flush_timer_ = nullptr;
  TestPlatformInterface mock_platform_;
};

TEST_F(PlatformStoreTest, Basic) {
  EXPECT_EQ(absl::nullopt, store_->get("foo"));
  store_->addOrUpdate("foo", "bar");
  EXPECT_EQ("bar", store_->get("foo").value());
  store_->addOrUpdate("foo", "eep");
  EXPECT_EQ("eep", store_->get("foo").value());
  store_->remove("foo");
  EXPECT_EQ(absl::nullopt, store_->get("foo"));
}

TEST_F(PlatformStoreTest, Persist) {
  store_->addOrUpdate("foo", "bar");
  store_->addOrUpdate("by\nz", "ee\np");
  ASSERT_TRUE(flush_timer_->enabled_);
  flush_timer_->invokeCallback(); // flush
  EXPECT_TRUE(flush_timer_->enabled_);
  // Not flushed as 5ms didn't pass.
  store_->addOrUpdate("baz", "eep");

  save_interval_ = std::chrono::seconds(0);
  createStore();
  KeyValueStore::ConstIterateCb validate = [](const std::string& key, const std::string&) {
    EXPECT_TRUE(key == "foo" || key == "by\nz");
    return KeyValueStore::Iterate::Continue;
  };

  EXPECT_EQ("bar", store_->get("foo").value());
  EXPECT_EQ("ee\np", store_->get("by\nz").value());
  EXPECT_FALSE(store_->get("baz").has_value());
  store_->iterate(validate);

  // This will flush due to 0ms flush interval
  store_->addOrUpdate("baz", "eep");
  createStore();
  EXPECT_TRUE(store_->get("baz").has_value());

  // This will flush due to 0ms flush interval
  store_->remove("bar");
  createStore();
  EXPECT_FALSE(store_->get("bar").has_value());
}

TEST_F(PlatformStoreTest, Iterate) {
  store_->addOrUpdate("foo", "bar");
  store_->addOrUpdate("baz", "eep");

  int full_counter = 0;
  KeyValueStore::ConstIterateCb validate = [&full_counter](const std::string& key,
                                                           const std::string&) {
    ++full_counter;
    EXPECT_TRUE(key == "foo" || key == "baz");
    return KeyValueStore::Iterate::Continue;
  };
  store_->iterate(validate);
  EXPECT_EQ(2, full_counter);

  int stop_early_counter = 0;
  KeyValueStore::ConstIterateCb stop_early = [&stop_early_counter](const std::string&,
                                                                   const std::string&) {
    ++stop_early_counter;
    return KeyValueStore::Iterate::Break;
  };
  store_->iterate(stop_early);
  EXPECT_EQ(1, stop_early_counter);
}

} // namespace
} // namespace KeyValue
} // namespace Extensions
} // namespace Envoy
