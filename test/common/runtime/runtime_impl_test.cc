#include <memory>
#include <string>

#include "common/runtime/runtime_impl.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;
using testing::_;

namespace Envoy {
namespace Runtime {

TEST(Random, DISABLED_benchmarkRandom) {
  Runtime::RandomGeneratorImpl random;

  for (size_t i = 0; i < 1000000000; ++i) {
    random.random();
  }
}

TEST(Random, sanityCheckOfUniquenessRandom) {
  Runtime::RandomGeneratorImpl random;
  std::set<uint64_t> results;
  const size_t num_of_results = 1000000;

  for (size_t i = 0; i < num_of_results; ++i) {
    results.insert(random.random());
  }

  EXPECT_EQ(num_of_results, results.size());
}

TEST(UUID, checkLengthOfUUID) {
  RandomGeneratorImpl random;

  std::string result = random.uuid();

  size_t expected_length = 36;
  EXPECT_EQ(expected_length, result.length());
}

TEST(UUID, sanityCheckOfUniqueness) {
  std::set<std::string> uuids;
  const size_t num_of_uuids = 100000;

  RandomGeneratorImpl random;
  for (size_t i = 0; i < num_of_uuids; ++i) {
    uuids.insert(random.uuid());
  }

  EXPECT_EQ(num_of_uuids, uuids.size());
}

class RuntimeImplTest : public testing::Test {
public:
  static void SetUpTestCase() {
    TestEnvironment::exec(
        {TestEnvironment::runfilesPath("test/common/runtime/filesystem_setup.sh")});
  }

  void setup() {
    EXPECT_CALL(dispatcher, createFilesystemWatcher_())
        .WillOnce(ReturnNew<NiceMock<Filesystem::MockWatcher>>());

    os_sys_calls_ = new NiceMock<Api::MockOsSysCalls>;
    ON_CALL(*os_sys_calls_, stat(_, _))
        .WillByDefault(
            Invoke([](const char* filename, struct stat* stat) { return ::stat(filename, stat); }));
  }

  void run(const std::string& primary_dir, const std::string& override_dir) {
    Api::OsSysCallsPtr os_sys_calls(os_sys_calls_);
    loader.reset(new LoaderImpl(dispatcher, tls, TestEnvironment::temporaryPath(primary_dir),
                                "envoy", override_dir, store, generator, std::move(os_sys_calls)));
  }

  Event::MockDispatcher dispatcher;
  NiceMock<ThreadLocal::MockInstance> tls;
  NiceMock<Api::MockOsSysCalls>* os_sys_calls_{};

  Stats::IsolatedStoreImpl store;
  MockRandomGenerator generator;
  std::unique_ptr<LoaderImpl> loader;
};

TEST_F(RuntimeImplTest, All) {
  setup();
  run("test/common/runtime/test_data/current", "envoy_override");

  // Basic string getting.
  EXPECT_EQ("world", loader->snapshot().get("file2"));
  EXPECT_EQ("hello\nworld", loader->snapshot().get("subdir.file3"));
  EXPECT_EQ("", loader->snapshot().get("invalid"));

  // Integer getting.
  EXPECT_EQ(1UL, loader->snapshot().getInteger("file1", 1));
  EXPECT_EQ(2UL, loader->snapshot().getInteger("file3", 1));
  EXPECT_EQ(123UL, loader->snapshot().getInteger("file4", 1));

  // Files with comments.
  EXPECT_EQ(123UL, loader->snapshot().getInteger("file5", 1));
  EXPECT_EQ("/home#about-us", loader->snapshot().get("file6"));
  EXPECT_EQ("", loader->snapshot().get("file7"));

  // Feature enablement.
  EXPECT_CALL(generator, random()).WillOnce(Return(1));
  EXPECT_TRUE(loader->snapshot().featureEnabled("file3", 1));

  EXPECT_CALL(generator, random()).WillOnce(Return(2));
  EXPECT_FALSE(loader->snapshot().featureEnabled("file3", 1));

  // Check stable value
  EXPECT_TRUE(loader->snapshot().featureEnabled("file3", 1, 1));
  EXPECT_FALSE(loader->snapshot().featureEnabled("file3", 1, 3));

  // Check stable value and num buckets.
  EXPECT_FALSE(loader->snapshot().featureEnabled("file4", 1, 200, 300));
  EXPECT_TRUE(loader->snapshot().featureEnabled("file4", 1, 122, 300));

  // Overrides from override dir
  EXPECT_EQ("hello override", loader->snapshot().get("file1"));
}

TEST_F(RuntimeImplTest, GetAll) {
  setup();
  run("test/common/runtime/test_data/current", "envoy_override");

  auto values = loader->snapshot().getAll();

  auto entry = values.find("file1");
  EXPECT_FALSE(entry == values.end());
  EXPECT_EQ("hello override", entry->second.string_value_);
  EXPECT_FALSE(entry->second.uint_value_);

  entry = values.find("file2");
  EXPECT_FALSE(entry == values.end());
  EXPECT_EQ("world", entry->second.string_value_);
  EXPECT_FALSE(entry->second.uint_value_);

  entry = values.find("file3");
  EXPECT_FALSE(entry == values.end());
  EXPECT_EQ("2", entry->second.string_value_);
  EXPECT_TRUE(entry->second.uint_value_);
  EXPECT_EQ(2UL, entry->second.uint_value_.value());

  entry = values.find("invalid");
  EXPECT_TRUE(entry == values.end());
}

TEST_F(RuntimeImplTest, BadDirectory) {
  setup();
  run("/baddir", "/baddir");
}

TEST_F(RuntimeImplTest, BadStat) {
  setup();
  EXPECT_CALL(*os_sys_calls_, stat(_, _)).WillOnce(Return(-1));
  run("test/common/runtime/test_data/current", "envoy_override");
  EXPECT_EQ(store.counter("runtime.load_error").value(), 1);
}

TEST_F(RuntimeImplTest, OverrideFolderDoesNotExist) {
  setup();
  run("test/common/runtime/test_data/current", "envoy_override_does_not_exist");

  EXPECT_EQ("hello", loader->snapshot().get("file1"));
}

TEST(NullRuntimeImplTest, All) {
  MockRandomGenerator generator;
  NullLoaderImpl loader(generator);
  EXPECT_EQ("", loader.snapshot().get("foo"));
  EXPECT_EQ(1UL, loader.snapshot().getInteger("foo", 1));
  EXPECT_CALL(generator, random()).WillOnce(Return(49));
  EXPECT_TRUE(loader.snapshot().featureEnabled("foo", 50));
  EXPECT_TRUE(loader.snapshot().getAll().empty());
}

} // namespace Runtime
} // namespace Envoy
