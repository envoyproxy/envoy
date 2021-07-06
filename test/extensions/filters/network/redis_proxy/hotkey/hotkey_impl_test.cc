#include <memory>
#include <string>

#include "source/extensions/filters/network/redis_proxy/hotkey/hotkey_impl.h"

#include "test/mocks/runtime/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace HotKey {

constexpr envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HotKey::CacheType
    hotkey_cache_type = envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HotKey::
        CacheType::RedisProxy_HotKey_CacheType_LFU;

class HotKeyCounterTest : public testing::Test {
public:
  void testIncrTryLockFailed(const HotKeyCounterSharedPtr& hk_counter, const std::string& key) {
    {
      Thread::LockGuard lock_guard(hk_counter->hotkey_cache_mutex_);
      hk_counter->incr(key);
    }
  }
};

TEST_F(HotKeyCounterTest, Name) {
  HotKeyCounterSharedPtr hk_counter = std::make_shared<HotKeyCounter>(hotkey_cache_type, 1);
  EXPECT_EQ(hk_counter->name(),
            fmt::format("{}_HotKeyCounter", static_cast<void*>(hk_counter.get())));
}

TEST_F(HotKeyCounterTest, GetHotKeys) {
  HotKeyCounterSharedPtr hk_counter = std::make_shared<HotKeyCounter>(hotkey_cache_type, 1);
  std::string test_key_1("test_key_1"), test_key_2("test_key_2");
  absl::flat_hash_map<std::string, uint32_t> cache;

  hk_counter->incr(test_key_1);
  EXPECT_EQ(1, hk_counter->getHotKeys(cache));
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(1, cache.at(test_key_1));
  cache.clear();
}

TEST_F(HotKeyCounterTest, Reset) {
  HotKeyCounterSharedPtr hk_counter = std::make_shared<HotKeyCounter>(hotkey_cache_type, 1);
  std::string test_key_1("test_key_1"), test_key_2("test_key_2");
  absl::flat_hash_map<std::string, uint32_t> cache;

  hk_counter->incr(test_key_1);
  hk_counter->getHotKeys(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(1, cache.at(test_key_1));
  cache.clear();

  hk_counter->reset();
  hk_counter->getHotKeys(cache);
  EXPECT_EQ(0, cache.size());
  cache.clear();
}

TEST_F(HotKeyCounterTest, Incr) {
  HotKeyCounterSharedPtr hk_counter = std::make_shared<HotKeyCounter>(hotkey_cache_type, 2);
  std::string test_key_1("test_key_1"), test_key_2("test_key_2");
  absl::flat_hash_map<std::string, uint32_t> cache;

  testIncrTryLockFailed(hk_counter, test_key_1);
  hk_counter->getHotKeys(cache);
  EXPECT_EQ(0, cache.size());
  cache.clear();

  hk_counter->incr(test_key_1);
  hk_counter->getHotKeys(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(1, cache.at(test_key_1));
  cache.clear();

  hk_counter->incr(test_key_1);
  hk_counter->incr(test_key_2);
  hk_counter->getHotKeys(cache);
  EXPECT_EQ(2, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(2, cache.at(test_key_1));
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(1, cache.at(test_key_2));
  cache.clear();
}

TEST(HotKeyCollectorTest, CreateHotKeyCounter) {
  Stats::TestUtil::TestStore store;
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HotKey hotkey_config;

  HotKeyCollectorSharedPtr test_default_hk_collector =
      std::make_shared<HotKeyCollector>(hotkey_config, *dispatcher, "", store);
  HotKeyCounterSharedPtr test_default_hk_counter_1 =
      test_default_hk_collector->createHotKeyCounter();
  EXPECT_EQ(true, bool(test_default_hk_collector));
  EXPECT_EQ(true, bool(test_default_hk_counter_1));

  hotkey_config.set_cache_type(hotkey_cache_type);
  hotkey_config.mutable_cache_capacity()->set_value(1);
  hotkey_config.mutable_collect_dispatch_interval()->set_nanos(500000000);
  HotKeyCollectorSharedPtr test_hk_collector =
      std::make_shared<HotKeyCollector>(hotkey_config, *dispatcher, "", store);
  HotKeyCounterSharedPtr test_hk_counter_1 = test_hk_collector->createHotKeyCounter();
  EXPECT_EQ(true, bool(test_hk_collector));
  EXPECT_EQ(true, bool(test_hk_counter_1));

  std::string test_key_1("test_key_1"), test_key_2("test_key_2");
  absl::flat_hash_map<std::string, uint32_t> cache;

  test_hk_collector->run();
  auto thread = Thread::threadFactoryForTest().createThread(
      [&]() { dispatcher->run(Event::Dispatcher::RunType::RunUntilExit); });

  test_hk_counter_1->incr(test_key_1);
  absl::SleepFor(absl::Milliseconds(550));
  test_hk_collector->getHotKeys(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(1, cache.at(test_key_1));
  cache.clear();

  test_hk_counter_1->incr(test_key_1);
  test_hk_counter_1->incr(test_key_2);
  test_hk_counter_1->getHotKeys(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(1, cache.at(test_key_2));
  cache.clear();

  test_hk_collector->destroyHotKeyCounter(test_hk_counter_1);

  dispatcher->exit();
  thread->join();
}

TEST(HotKeyCollectorTest, DestroyHotKeyCounter) {
  Stats::TestUtil::TestStore store;
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HotKey hotkey_config;
  hotkey_config.set_cache_type(hotkey_cache_type);
  hotkey_config.mutable_cache_capacity()->set_value(1);
  hotkey_config.mutable_collect_dispatch_interval()->set_nanos(500000000);
  HotKeyCollectorSharedPtr test_hk_collector =
      std::make_shared<HotKeyCollector>(hotkey_config, *dispatcher, "", store);
  HotKeyCounterSharedPtr test_hk_counter_1 = test_hk_collector->createHotKeyCounter();
  std::string test_key_1("test_key_1");
  absl::flat_hash_map<std::string, uint32_t> cache;

  test_hk_collector->run();
  auto thread = Thread::threadFactoryForTest().createThread(
      [&]() { dispatcher->run(Event::Dispatcher::RunType::RunUntilExit); });

  test_hk_counter_1->incr(test_key_1);
  absl::SleepFor(absl::Milliseconds(550));
  test_hk_collector->getHotKeys(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(1, cache.at(test_key_1));
  cache.clear();

  test_hk_counter_1->incr(test_key_1);
  test_hk_collector->destroyHotKeyCounter(test_hk_counter_1);
  EXPECT_EQ(nullptr, test_hk_counter_1);
  absl::SleepFor(absl::Milliseconds(550));
  test_hk_collector->getHotKeys(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(2, cache.at(test_key_1));
  cache.clear();

  test_hk_collector->destroyHotKeyCounter(test_hk_counter_1);

  dispatcher->exit();
  thread->join();
}

TEST(HotKeyCollectorTest, Run) {
  Stats::TestUtil::TestStore store;
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HotKey hotkey_config;
  hotkey_config.set_cache_type(hotkey_cache_type);
  hotkey_config.mutable_cache_capacity()->set_value(1);
  hotkey_config.mutable_collect_dispatch_interval()->set_nanos(500000000);
  hotkey_config.mutable_attenuate_dispatch_interval()->set_nanos(500000000);
  hotkey_config.mutable_attenuate_cache_interval()->set_seconds(3);
  HotKeyCollectorSharedPtr test_hk_collector =
      std::make_shared<HotKeyCollector>(hotkey_config, *dispatcher, "", store);
  HotKeyCounterSharedPtr test_hk_counter_1 = test_hk_collector->createHotKeyCounter();
  std::string test_key_1("test_key_1"), test_key_2("test_key_2");
  absl::flat_hash_map<std::string, uint32_t> cache;

  test_hk_counter_1->incr(test_key_1);
  absl::SleepFor(absl::Milliseconds(550));
  test_hk_collector->getHotKeys(cache);
  EXPECT_EQ(0, cache.size());
  cache.clear();

  test_hk_collector->run();
  auto thread = Thread::threadFactoryForTest().createThread(
      [&]() { dispatcher->run(Event::Dispatcher::RunType::RunUntilExit); });

  test_hk_counter_1->incr(test_key_1);
  absl::SleepFor(absl::Milliseconds(550));
  test_hk_counter_1->getHotKeys(cache);
  EXPECT_EQ(0, cache.size());
  cache.clear();
  test_hk_collector->getHotKeys(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(2, cache.at(test_key_1));
  cache.clear();

  test_hk_counter_1->incr(test_key_2);
  absl::SleepFor(absl::Milliseconds(550));
  test_hk_collector->getHotKeys(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(2, cache.at(test_key_1));
  cache.clear();

  test_hk_counter_1->incr(test_key_2);
  absl::SleepFor(absl::Milliseconds(550));
  test_hk_collector->getHotKeys(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(2, cache.at(test_key_2));
  cache.clear();

  absl::SleepFor(absl::Milliseconds(3050));
  test_hk_collector->getHotKeys(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(1, cache.at(test_key_2));
  cache.clear();

  test_hk_collector->destroyHotKeyCounter(test_hk_counter_1);

  dispatcher->exit();
  thread->join();
}

TEST(HotKeyCollectorTest, GetHotKeys) {
  Stats::TestUtil::TestStore store;
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HotKey hotkey_config;
  hotkey_config.set_cache_type(hotkey_cache_type);
  hotkey_config.mutable_cache_capacity()->set_value(3);
  hotkey_config.mutable_collect_dispatch_interval()->set_nanos(500000000);
  HotKeyCollectorSharedPtr test_hk_collector =
      std::make_shared<HotKeyCollector>(hotkey_config, *dispatcher, "", store);
  HotKeyCounterSharedPtr test_hk_counter_1 = test_hk_collector->createHotKeyCounter();
  HotKeyCounterSharedPtr test_hk_counter_2 = test_hk_collector->createHotKeyCounter();
  std::string test_key_1("test_key_1"), test_key_2("test_key_2"), test_key_3("test_key_3");
  absl::flat_hash_map<std::string, uint32_t> cache;

  test_hk_collector->run();
  auto thread = Thread::threadFactoryForTest().createThread(
      [&]() { dispatcher->run(Event::Dispatcher::RunType::RunUntilExit); });

  test_hk_counter_1->incr(test_key_1);
  test_hk_counter_1->incr(test_key_2);
  test_hk_counter_2->incr(test_key_1);
  test_hk_counter_2->incr(test_key_3);
  absl::SleepFor(absl::Milliseconds(550));
  EXPECT_EQ(3, test_hk_collector->getHotKeys(cache));
  EXPECT_EQ(3, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(2, cache.at(test_key_1));
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(1, cache.at(test_key_2));
  EXPECT_EQ(1, cache.count(test_key_3));
  EXPECT_EQ(1, cache.at(test_key_3));
  cache.clear();

  test_hk_collector->destroyHotKeyCounter(test_hk_counter_1);
  test_hk_collector->destroyHotKeyCounter(test_hk_counter_2);

  dispatcher->exit();
  thread->join();
}

TEST(HotKeyCollectorTest, GetHotKeyHeats) {
  Stats::TestUtil::TestStore store;
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HotKey hotkey_config;
  hotkey_config.set_cache_type(hotkey_cache_type);
  hotkey_config.mutable_cache_capacity()->set_value(3);
  hotkey_config.mutable_collect_dispatch_interval()->set_nanos(500000000);
  HotKeyCollectorSharedPtr test_hk_collector =
      std::make_shared<HotKeyCollector>(hotkey_config, *dispatcher, "", store);
  HotKeyCounterSharedPtr test_hk_counter_1 = test_hk_collector->createHotKeyCounter();
  HotKeyCounterSharedPtr test_hk_counter_2 = test_hk_collector->createHotKeyCounter();
  std::string test_key_1("test_key_1"), test_key_2("test_key_2"), test_key_3("test_key_3");
  absl::flat_hash_map<std::string, uint32_t> cache;

  test_hk_collector->run();
  auto thread = Thread::threadFactoryForTest().createThread(
      [&]() { dispatcher->run(Event::Dispatcher::RunType::RunUntilExit); });

  test_hk_counter_1->incr(test_key_1);
  for (uint8_t i = 0; i < 44; ++i) {
    test_hk_counter_1->incr(test_key_2);
    test_hk_counter_2->incr(test_key_2);
    test_hk_counter_2->incr(test_key_3);
  }
  absl::SleepFor(absl::Milliseconds(550));
  EXPECT_EQ(3, test_hk_collector->getHotKeyHeats(cache));
  EXPECT_EQ(3, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(1, cache.at(test_key_1));
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(52, cache.at(test_key_2));
  EXPECT_EQ(1, cache.count(test_key_3));
  EXPECT_EQ(44, cache.at(test_key_3));
  cache.clear();

  test_hk_collector->destroyHotKeyCounter(test_hk_counter_1);
  test_hk_collector->destroyHotKeyCounter(test_hk_counter_2);

  dispatcher->exit();
  thread->join();
}

} // namespace HotKey
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
