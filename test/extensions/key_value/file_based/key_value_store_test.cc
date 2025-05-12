#include <memory>
#include <string>

#include "source/common/common/key_value_store_base.h"
#include "source/extensions/key_value/file_based/config.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/file_system_for_test.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace KeyValue {
namespace {

class KeyValueStoreTest : public testing::Test {
protected:
  KeyValueStoreTest() : filename_(TestEnvironment::temporaryPath("key_value_store")) {
    TestEnvironment::removePath(filename_);
    createStore();
  }

  void createStore(uint32_t max_entries = 0) {
    // Note that timer assignment (to ttl vs flush) is determined by their ordering here
    ttl_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
    flush_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
    store_ = std::make_unique<FileBasedKeyValueStore>(
        dispatcher_, flush_interval_, Filesystem::fileSystemForTest(), filename_, max_entries);
  }
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::string filename_;
  std::unique_ptr<FileBasedKeyValueStore> store_{};
  std::chrono::seconds flush_interval_{5};
  Event::MockTimer* ttl_timer_ = nullptr;
  Event::MockTimer* flush_timer_ = nullptr;
  Event::SimulatedTimeSystem test_time_;
};

TEST_F(KeyValueStoreTest, Basic) {
  EXPECT_EQ(absl::nullopt, store_->get("foo"));
  store_->addOrUpdate("foo", "bar", absl::nullopt);
  EXPECT_EQ("bar", store_->get("foo").value());
  store_->addOrUpdate("foo", "eep", absl::nullopt);
  EXPECT_EQ("eep", store_->get("foo").value());
  store_->remove("foo");
  EXPECT_EQ(absl::nullopt, store_->get("foo"));
}

TEST_F(KeyValueStoreTest, Ttl) {
  test_time_.setSystemTime(std::chrono::milliseconds(0));
  EXPECT_CALL(*ttl_timer_, enableTimer(std::chrono::milliseconds(5000), _));
  store_->addOrUpdate("foo", "bar", std::chrono::seconds(5));
  EXPECT_EQ("bar", store_->get("foo").value());
  // Advance time to when the timer is scheduled to fire
  test_time_.setSystemTime(std::chrono::milliseconds(5000));
  ttl_timer_->invokeCallback();
  EXPECT_EQ(absl::nullopt, store_->get("foo"));
}

TEST_F(KeyValueStoreTest, TtlUpdate) {
  test_time_.setSystemTime(std::chrono::milliseconds(0));
  EXPECT_CALL(*ttl_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  EXPECT_CALL(*ttl_timer_, enableTimer(std::chrono::milliseconds(5000), _));
  store_->addOrUpdate("foo", "bar", std::chrono::seconds(10));
  test_time_.setSystemTime(std::chrono::milliseconds(1000));
  store_->addOrUpdate("foo", "bar", std::chrono::seconds(5));
  // Advance time to when the timer is scheduled to fire
  test_time_.setSystemTime(std::chrono::milliseconds(6000));
  ttl_timer_->invokeCallback();
  EXPECT_EQ(absl::nullopt, store_->get("foo"));
}

TEST_F(KeyValueStoreTest, TtlUpdateToNull) {
  test_time_.setSystemTime(std::chrono::milliseconds(0));
  store_->addOrUpdate("foo", "bar", std::chrono::seconds(5));
  store_->addOrUpdate("foo", "bar", absl::nullopt);
  // Expect timer is disabled
  EXPECT_FALSE(ttl_timer_->enabled());
}

TEST_F(KeyValueStoreTest, TtlUpdateFromNull) {
  test_time_.setSystemTime(std::chrono::milliseconds(0));
  store_->addOrUpdate("foo", "bar", absl::nullopt);
  EXPECT_CALL(*ttl_timer_, enableTimer(std::chrono::milliseconds(5000), _));
  store_->addOrUpdate("foo", "bar", std::chrono::seconds(5));
  // Advance time to when the timer is scheduled to fire
  test_time_.setSystemTime(std::chrono::milliseconds(10000));
  ttl_timer_->invokeCallback();
  EXPECT_EQ(absl::nullopt, store_->get("foo"));
}

TEST_F(KeyValueStoreTest, TtlRemove) {
  test_time_.setSystemTime(std::chrono::milliseconds(0));
  store_->addOrUpdate("foo", "bar", std::chrono::seconds(5));
  store_->remove("foo");
  EXPECT_FALSE(ttl_timer_->enabled());
}

TEST_F(KeyValueStoreTest, MaxEntries) {
  createStore(2);
  // Add 2 entries.
  store_->addOrUpdate("1", "a", absl::nullopt);
  store_->addOrUpdate("2", "b", absl::nullopt);
  EXPECT_EQ("a", store_->get("1").value());
  EXPECT_EQ("b", store_->get("2").value());

  // Adding '3' should evict '1'.
  store_->addOrUpdate("3", "c", absl::nullopt);
  EXPECT_EQ("c", store_->get("3").value());
  EXPECT_EQ("b", store_->get("2").value());
  EXPECT_EQ(absl::nullopt, store_->get("1"));
}

TEST_F(KeyValueStoreTest, Persist) {
  store_->addOrUpdate("foo", "bar", absl::nullopt);
  store_->addOrUpdate("ba\nz", "ee\np", absl::nullopt);
  ASSERT_TRUE(flush_timer_->enabled_);
  flush_timer_->invokeCallback(); // flush
  EXPECT_TRUE(flush_timer_->enabled_);
  // Not flushed as 5ms didn't pass.
  store_->addOrUpdate("baz", "eep", absl::nullopt);

  flush_interval_ = std::chrono::seconds(0);
  createStore();
  KeyValueStore::ConstIterateCb validate = [](const std::string& key, const std::string&) {
    EXPECT_TRUE(key == "foo" || key == "ba\nz");
    return KeyValueStore::Iterate::Continue;
  };

  EXPECT_EQ("bar", store_->get("foo").value());
  EXPECT_EQ("ee\np", store_->get("ba\nz").value());
  EXPECT_FALSE(store_->get("baz").has_value());
  store_->iterate(validate);

  // This will flush due to 0ms flush interval
  store_->addOrUpdate("baz", "eep", absl::nullopt);
  createStore();
  EXPECT_TRUE(store_->get("baz").has_value());

  // This will flush due to 0ms flush interval
  store_->remove("bar");
  createStore();
  EXPECT_FALSE(store_->get("bar").has_value());
}

TEST_F(KeyValueStoreTest, PersistWithTTL) {
  test_time_.setSystemTime(std::chrono::milliseconds(0));
  store_->addOrUpdate("foo", "bar", std::chrono::seconds(2));
  store_->addOrUpdate("ee", "ba", std::chrono::seconds(1));
  flush_timer_->invokeCallback(); // flush manually
  test_time_.setSystemTime(std::chrono::milliseconds(1000));
  // Keys should expire based on the absolute time.
  // 'ee' should expire on load because it's been 1 second.
  createStore();
  EXPECT_EQ("bar", store_->get("foo").value());
  EXPECT_EQ(absl::nullopt, store_->get("ee"));
  test_time_.setMonotonicTime(std::chrono::milliseconds(2000));
  ttl_timer_->invokeCallback();
  EXPECT_EQ(absl::nullopt, store_->get("foo"));
}

TEST_F(KeyValueStoreTest, Iterate) {
  store_->addOrUpdate("foo", "bar", absl::nullopt);
  store_->addOrUpdate("baz", "eep", absl::nullopt);

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

#ifndef NDEBUG
TEST_F(KeyValueStoreTest, ShouldCrashIfIterateCallbackAddsOrUpdatesStore) {
  store_->addOrUpdate("foo", "bar", absl::nullopt);
  store_->addOrUpdate("baz", "eep", absl::nullopt);
  KeyValueStore::ConstIterateCb update_value_callback = [this](const std::string& key,
                                                               const std::string&) {
    EXPECT_TRUE(key == "foo" || key == "baz");
    store_->addOrUpdate("foo", "updated-bar", absl::nullopt);
    return KeyValueStore::Iterate::Continue;
  };

  EXPECT_DEATH(store_->iterate(update_value_callback), "addOrUpdate under the stack of iterate");

  KeyValueStore::ConstIterateCb add_key_callback = [this](const std::string& key,
                                                          const std::string&) {
    EXPECT_TRUE(key == "foo" || key == "baz" || key == "new-key");
    if (key == "foo") {
      store_->addOrUpdate("new-key", "new-value", absl::nullopt);
    }
    return KeyValueStore::Iterate::Continue;
  };
  EXPECT_DEATH(store_->iterate(add_key_callback), "addOrUpdate under the stack of iterate");
}
#endif

TEST_F(KeyValueStoreTest, HandleBadFile) {
  auto checkBadFile = [this](std::string file, std::string error) {
    TestEnvironment::writeStringToFileForTest(filename_, file, true);
    EXPECT_LOG_CONTAINS("warn", error, createStore());

    // File will be parsed up until error.
    EXPECT_EQ("bar", store_->get("foo").value());
  };
  checkBadFile("3\nfoo3\nbar3", "Bad file: no newline");
  checkBadFile("3\nfoo3\nbar\n", "Bad file: no length");
  checkBadFile("3\nfoo3\nbarasd\n", "Bad file: no length");
  checkBadFile("3\nfoo3\nbar3\na", "Bad file: insufficient contents");
}

#ifndef WIN32
TEST_F(KeyValueStoreTest, HandleInvalidFile) {
  filename_ = TestEnvironment::temporaryPath("some/unlikely/bad/path/bar");
  createStore();
  EXPECT_LOG_CONTAINS("error", "Failed to flush cache to file " + filename_, store_->flush());
}
#endif

} // namespace
} // namespace KeyValue
} // namespace Extensions
} // namespace Envoy
