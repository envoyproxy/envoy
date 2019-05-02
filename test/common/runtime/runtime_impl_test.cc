#include <memory>
#include <string>

#include "common/runtime/runtime_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;

namespace Envoy {
namespace Runtime {
namespace {

TEST(Random, DISABLED_benchmarkRandom) {
  Runtime::RandomGeneratorImpl random;

  for (size_t i = 0; i < 1000000000; ++i) {
    random.random();
  }
}

TEST(Random, SanityCheckOfUniquenessRandom) {
  Runtime::RandomGeneratorImpl random;
  std::set<uint64_t> results;
  const size_t num_of_results = 1000000;

  for (size_t i = 0; i < num_of_results; ++i) {
    results.insert(random.random());
  }

  EXPECT_EQ(num_of_results, results.size());
}

TEST(UUID, CheckLengthOfUUID) {
  RandomGeneratorImpl random;

  std::string result = random.uuid();

  size_t expected_length = 36;
  EXPECT_EQ(expected_length, result.length());
}

TEST(UUID, SanityCheckOfUniqueness) {
  std::set<std::string> uuids;
  const size_t num_of_uuids = 100000;

  RandomGeneratorImpl random;
  for (size_t i = 0; i < num_of_uuids; ++i) {
    uuids.insert(random.uuid());
  }

  EXPECT_EQ(num_of_uuids, uuids.size());
}

class DiskBackedLoaderImplTest : public testing::Test {
protected:
  DiskBackedLoaderImplTest() : api_(Api::createApiForTest(store_)) {}

  static void SetUpTestSuite() {
    TestEnvironment::exec(
        {TestEnvironment::runfilesPath("test/common/runtime/filesystem_setup.sh")});
  }

  void setup() {
    EXPECT_CALL(dispatcher_, createFilesystemWatcher_()).WillOnce(InvokeWithoutArgs([this] {
      Filesystem::MockWatcher* mock_watcher = new NiceMock<Filesystem::MockWatcher>();
      EXPECT_CALL(*mock_watcher, addWatch(_, Filesystem::Watcher::Events::MovedTo, _))
          .WillOnce(Invoke([this](const std::string&, uint32_t,
                                  Filesystem::Watcher::OnChangedCb cb) { on_changed_cb_ = cb; }));
      return mock_watcher;
    }));
  }

  void run(const std::string& primary_dir, const std::string& override_dir) {
    loader_ = std::make_unique<DiskBackedLoaderImpl>(
        dispatcher_, tls_, base_, TestEnvironment::temporaryPath(primary_dir), "envoy",
        override_dir, store_, generator_, *api_);
  }

  void write(const std::string& path, const std::string& value) {
    TestEnvironment::writeStringToFileForTest(path, value);
  }

  void updateDiskLayer() { on_changed_cb_(Filesystem::Watcher::Events::MovedTo); }

  Event::MockDispatcher dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;

  Filesystem::Watcher::OnChangedCb on_changed_cb_;
  Stats::IsolatedStoreImpl store_;
  MockRandomGenerator generator_;
  std::unique_ptr<LoaderImpl> loader_;
  Api::ApiPtr api_;
  ProtobufWkt::Struct base_;
};

TEST_F(DiskBackedLoaderImplTest, All) {
  setup();
  run("test/common/runtime/test_data/current", "envoy_override");

  // Basic string getting.
  EXPECT_EQ("world", loader_->snapshot().get("file2"));
  EXPECT_EQ("hello\nworld", loader_->snapshot().get("subdir.file3"));
  EXPECT_EQ("", loader_->snapshot().get("invalid"));

  // Integer getting.
  EXPECT_EQ(1UL, loader_->snapshot().getInteger("file1", 1));
  EXPECT_EQ(2UL, loader_->snapshot().getInteger("file3", 1));
  EXPECT_EQ(123UL, loader_->snapshot().getInteger("file4", 1));

  // Boolean getting.
  bool value;
  SnapshotImpl* snapshot = reinterpret_cast<SnapshotImpl*>(&loader_->snapshot());

  EXPECT_EQ(true, snapshot->getBoolean("file11", value));
  EXPECT_EQ(true, value);
  EXPECT_EQ(true, snapshot->getBoolean("file12", value));
  EXPECT_EQ(false, value);
  EXPECT_EQ(true, snapshot->getBoolean("file13", value));
  EXPECT_EQ(true, value);
  // File1 is not a boolean.
  EXPECT_EQ(false, snapshot->getBoolean("file1", value));

  // Feature defaults.
  // test_feature_true is explicitly set true in runtime_features.cc
  EXPECT_EQ(true, snapshot->runtimeFeatureEnabled("envoy.reloadable_features.test_feature_true"));
  // test_feature_false is not in runtime_features.cc and so is false by default.
  EXPECT_EQ(false, snapshot->runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));

  // Feature defaults via helper function.
  EXPECT_EQ(false, runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));
  EXPECT_EQ(true, runtimeFeatureEnabled("envoy.reloadable_features.test_feature_true"));

  // Files with comments.
  EXPECT_EQ(123UL, loader_->snapshot().getInteger("file5", 1));
  EXPECT_EQ("/home#about-us", loader_->snapshot().get("file6"));
  EXPECT_EQ("", loader_->snapshot().get("file7"));

  // Feature enablement.
  EXPECT_CALL(generator_, random()).WillOnce(Return(1));
  EXPECT_TRUE(loader_->snapshot().featureEnabled("file3", 1));

  EXPECT_CALL(generator_, random()).WillOnce(Return(2));
  EXPECT_FALSE(loader_->snapshot().featureEnabled("file3", 1));

  // Fractional percent feature enablement
  envoy::type::FractionalPercent fractional_percent;
  fractional_percent.set_numerator(5);
  fractional_percent.set_denominator(envoy::type::FractionalPercent::TEN_THOUSAND);

  EXPECT_CALL(generator_, random()).WillOnce(Return(50));
  EXPECT_TRUE(loader_->snapshot().featureEnabled("file8", fractional_percent)); // valid data

  EXPECT_CALL(generator_, random()).WillOnce(Return(60));
  EXPECT_FALSE(loader_->snapshot().featureEnabled("file8", fractional_percent)); // valid data

  // We currently expect that runtime values represented as fractional percents that are provided as
  // integers are parsed simply as percents (denominator of 100).
  EXPECT_CALL(generator_, random()).WillOnce(Return(53));
  EXPECT_FALSE(loader_->snapshot().featureEnabled("file10", fractional_percent)); // valid int data
  EXPECT_CALL(generator_, random()).WillOnce(Return(51));
  EXPECT_TRUE(loader_->snapshot().featureEnabled("file10", fractional_percent)); // valid int data

  EXPECT_CALL(generator_, random()).WillOnce(Return(4));
  EXPECT_TRUE(
      loader_->snapshot().featureEnabled("file9", fractional_percent)); // invalid proto data

  EXPECT_CALL(generator_, random()).WillOnce(Return(6));
  EXPECT_FALSE(
      loader_->snapshot().featureEnabled("file9", fractional_percent)); // invalid proto data

  EXPECT_CALL(generator_, random()).WillOnce(Return(4));
  EXPECT_TRUE(loader_->snapshot().featureEnabled("file1", fractional_percent)); // invalid data

  EXPECT_CALL(generator_, random()).WillOnce(Return(6));
  EXPECT_FALSE(loader_->snapshot().featureEnabled("file1", fractional_percent)); // invalid data

  // Check stable value
  EXPECT_TRUE(loader_->snapshot().featureEnabled("file3", 1, 1));
  EXPECT_FALSE(loader_->snapshot().featureEnabled("file3", 1, 3));

  // Check stable value and num buckets.
  EXPECT_FALSE(loader_->snapshot().featureEnabled("file4", 1, 200, 300));
  EXPECT_TRUE(loader_->snapshot().featureEnabled("file4", 1, 122, 300));

  // Overrides from override dir
  EXPECT_EQ("hello override", loader_->snapshot().get("file1"));
}

TEST_F(DiskBackedLoaderImplTest, GetLayers) {
  base_ = TestUtility::parseYaml<ProtobufWkt::Struct>(R"EOF(
    foo: whatevs
  )EOF");
  setup();
  run("test/common/runtime/test_data/current", "envoy_override");
  const auto& layers = loader_->snapshot().getLayers();
  EXPECT_EQ(4, layers.size());
  EXPECT_EQ("whatevs", layers[0]->values().find("foo")->second.raw_string_value_);
  EXPECT_EQ("hello", layers[1]->values().find("file1")->second.raw_string_value_);
  EXPECT_EQ("hello override", layers[2]->values().find("file1")->second.raw_string_value_);
  // Admin should be last
  EXPECT_NE(nullptr, dynamic_cast<const AdminLayer*>(layers.back().get()));
  EXPECT_TRUE(layers[3]->values().empty());

  loader_->mergeValues({{"foo", "bar"}});
  // The old snapshot and its layers should have been invalidated. Refetch.
  const auto& new_layers = loader_->snapshot().getLayers();
  EXPECT_EQ("bar", new_layers[3]->values().find("foo")->second.raw_string_value_);
}

TEST_F(DiskBackedLoaderImplTest, BadDirectory) {
  setup();
  run("/baddir", "/baddir");
}

TEST_F(DiskBackedLoaderImplTest, OverrideFolderDoesNotExist) {
  setup();
  run("test/common/runtime/test_data/current", "envoy_override_does_not_exist");

  EXPECT_EQ("hello", loader_->snapshot().get("file1"));
}

TEST_F(DiskBackedLoaderImplTest, PercentHandling) {
  setup();
  run("test/common/runtime/test_data/current", "envoy_override");

  envoy::type::FractionalPercent default_value;

  // Smoke test integer value of 0, should be interpreted as 0%
  {
    loader_->mergeValues({{"foo", "0"}});

    EXPECT_FALSE(loader_->snapshot().featureEnabled("foo", default_value, 0));
    EXPECT_FALSE(loader_->snapshot().featureEnabled("foo", default_value, 5));
  }

  // Smoke test integer value of 5, should be interpreted as 5%
  {
    loader_->mergeValues({{"foo", "5"}});
    EXPECT_TRUE(loader_->snapshot().featureEnabled("foo", default_value, 0));
    EXPECT_TRUE(loader_->snapshot().featureEnabled("foo", default_value, 4));
    EXPECT_FALSE(loader_->snapshot().featureEnabled("foo", default_value, 5));
    EXPECT_TRUE(loader_->snapshot().featureEnabled("foo", default_value, 100));
  }

  // Verify uint64 -> uint32 conversion by using a runtime value with all 0s in
  // the bottom 32 bits. If it were to be naively treated as a uint32_t then it
  // would appear as 0%, but it should be 100% because we assume the
  // denominator is 100
  {
    // NOTE: high_value has to have the property that the lowest 32 bits % 100
    // is less than 100. If it's greater than 100 the test will pass whether or
    // not the uint32 conversion is handled properly.
    uint64_t high_value = 1UL << 60;
    std::string high_value_str = std::to_string(high_value);
    loader_->mergeValues({{"foo", high_value_str}});
    EXPECT_TRUE(loader_->snapshot().featureEnabled("foo", default_value, 0));
    EXPECT_TRUE(loader_->snapshot().featureEnabled("foo", default_value, 50));
    EXPECT_TRUE(loader_->snapshot().featureEnabled("foo", default_value, 100));
    EXPECT_TRUE(loader_->snapshot().featureEnabled("foo", default_value, 12389));
    EXPECT_TRUE(loader_->snapshot().featureEnabled("foo", default_value, 23859235));
  }
}

void testNewOverrides(Loader& loader, Stats::Store& store) {
  // New string
  loader.mergeValues({{"foo", "bar"}});
  EXPECT_EQ("bar", loader.snapshot().get("foo"));
  EXPECT_EQ(1, store.gauge("runtime.admin_overrides_active").value());

  // Remove new string
  loader.mergeValues({{"foo", ""}});
  EXPECT_EQ("", loader.snapshot().get("foo"));
  EXPECT_EQ(0, store.gauge("runtime.admin_overrides_active").value());

  // New integer
  loader.mergeValues({{"baz", "42"}});
  EXPECT_EQ(42, loader.snapshot().getInteger("baz", 0));
  EXPECT_EQ(1, store.gauge("runtime.admin_overrides_active").value());

  // Remove new integer
  loader.mergeValues({{"baz", ""}});
  EXPECT_EQ(0, loader.snapshot().getInteger("baz", 0));
  EXPECT_EQ(0, store.gauge("runtime.admin_overrides_active").value());
}

TEST_F(DiskBackedLoaderImplTest, MergeValues) {
  setup();
  run("test/common/runtime/test_data/current", "envoy_override");
  testNewOverrides(*loader_, store_);

  // Override string
  loader_->mergeValues({{"file2", "new world"}});
  EXPECT_EQ("new world", loader_->snapshot().get("file2"));
  EXPECT_EQ(1, store_.gauge("runtime.admin_overrides_active").value());

  // Remove overridden string
  loader_->mergeValues({{"file2", ""}});
  EXPECT_EQ("world", loader_->snapshot().get("file2"));
  EXPECT_EQ(0, store_.gauge("runtime.admin_overrides_active").value());

  // Override integer
  loader_->mergeValues({{"file3", "42"}});
  EXPECT_EQ(42, loader_->snapshot().getInteger("file3", 1));
  EXPECT_EQ(1, store_.gauge("runtime.admin_overrides_active").value());

  // Remove overridden integer
  loader_->mergeValues({{"file3", ""}});
  EXPECT_EQ(2, loader_->snapshot().getInteger("file3", 1));
  EXPECT_EQ(0, store_.gauge("runtime.admin_overrides_active").value());

  // Override override string
  loader_->mergeValues({{"file1", "hello overridden override"}});
  EXPECT_EQ("hello overridden override", loader_->snapshot().get("file1"));
  EXPECT_EQ(1, store_.gauge("runtime.admin_overrides_active").value());

  // Remove overridden override string
  loader_->mergeValues({{"file1", ""}});
  EXPECT_EQ("hello override", loader_->snapshot().get("file1"));
  EXPECT_EQ(0, store_.gauge("runtime.admin_overrides_active").value());
}

// Validate that admin overrides disk, disk overrides bootstrap.
TEST_F(DiskBackedLoaderImplTest, LayersOverride) {
  base_ = TestUtility::parseYaml<ProtobufWkt::Struct>(R"EOF(
    some: thing
    other: thang
    file2: whatevs
  )EOF");
  setup();
  run("test/common/runtime/test_data/current", "envoy_override");
  // Disk overrides bootstrap.
  EXPECT_EQ("world", loader_->snapshot().get("file2"));
  EXPECT_EQ("thing", loader_->snapshot().get("some"));
  EXPECT_EQ("thang", loader_->snapshot().get("other"));
  // Admin overrides disk and bootstrap.
  loader_->mergeValues({{"file2", "pluto"}, {"some", "day soon"}});
  EXPECT_EQ("pluto", loader_->snapshot().get("file2"));
  EXPECT_EQ("day soon", loader_->snapshot().get("some"));
  EXPECT_EQ("thang", loader_->snapshot().get("other"));
  // Admin overrides stick over filesystem updates.
  EXPECT_EQ("Layer cake", loader_->snapshot().get("file14"));
  EXPECT_EQ("Cheese cake", loader_->snapshot().get("file15"));
  loader_->mergeValues({{"file14", "Mega layer cake"}});
  EXPECT_EQ("Mega layer cake", loader_->snapshot().get("file14"));
  EXPECT_EQ("Cheese cake", loader_->snapshot().get("file15"));
  write("test/common/runtime/test_data/current/envoy/file14", "Sad cake");
  write("test/common/runtime/test_data/current/envoy/file15", "Happy cake");
  updateDiskLayer();
  EXPECT_EQ("Mega layer cake", loader_->snapshot().get("file14"));
  EXPECT_EQ("Happy cake", loader_->snapshot().get("file15"));
}

class LoaderImplTest : public testing::Test {
protected:
  void setup() { loader_ = std::make_unique<LoaderImpl>(base_, generator_, store_, tls_); }

  MockRandomGenerator generator_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::IsolatedStoreImpl store_;
  std::unique_ptr<LoaderImpl> loader_;
  ProtobufWkt::Struct base_;
};

TEST_F(LoaderImplTest, All) {
  setup();
  EXPECT_EQ("", loader_->snapshot().get("foo"));
  EXPECT_EQ(1UL, loader_->snapshot().getInteger("foo", 1));
  EXPECT_CALL(generator_, random()).WillOnce(Return(49));
  EXPECT_TRUE(loader_->snapshot().featureEnabled("foo", 50));
  testNewOverrides(*loader_, store_);
}

// Validate proto parsing sanity.
TEST_F(LoaderImplTest, ProtoParsing) {
  base_ = TestUtility::parseYaml<ProtobufWkt::Struct>(R"EOF(
    file1: hello override
    file2: world
    file3: 2
    file4: 123
    file8:
      numerator: 52
      denominator: HUNDRED
    file9:
      numerator: 100
      denominator: NONSENSE
    file10: 52
    file11: true
    file12: FaLSe
    file13: false
    subdir:
      file3: "hello\nworld"
    numerator_only:
      numerator: 52
    denominator_only:
      denominator: HUNDRED
    false_friend:
      numerator: 100
      foo: bar
    empty: {}
  )EOF");
  setup();

  // Basic string getting.
  EXPECT_EQ("world", loader_->snapshot().get("file2"));
  EXPECT_EQ("hello\nworld", loader_->snapshot().get("subdir.file3"));
  EXPECT_EQ("", loader_->snapshot().get("invalid"));

  // Integer getting.
  EXPECT_EQ(1UL, loader_->snapshot().getInteger("file1", 1));
  EXPECT_EQ(2UL, loader_->snapshot().getInteger("file3", 1));
  EXPECT_EQ(123UL, loader_->snapshot().getInteger("file4", 1));

  // Boolean getting.
  bool value;
  SnapshotImpl* snapshot = reinterpret_cast<SnapshotImpl*>(&loader_->snapshot());

  EXPECT_EQ(true, snapshot->getBoolean("file11", value));
  EXPECT_EQ(true, value);
  EXPECT_EQ(true, snapshot->getBoolean("file12", value));
  EXPECT_EQ(false, value);
  EXPECT_EQ(true, snapshot->getBoolean("file13", value));
  EXPECT_EQ(false, value);
  // File1 is not a boolean.
  EXPECT_EQ(false, snapshot->getBoolean("file1", value));
  // Neither is blah.blah
  EXPECT_EQ(false, snapshot->getBoolean("blah.blah", value));

  // Fractional percent feature enablement
  envoy::type::FractionalPercent fractional_percent;
  fractional_percent.set_numerator(5);
  fractional_percent.set_denominator(envoy::type::FractionalPercent::TEN_THOUSAND);

  EXPECT_CALL(generator_, random()).WillOnce(Return(50));
  EXPECT_TRUE(loader_->snapshot().featureEnabled("file8", fractional_percent)); // valid data
  EXPECT_CALL(generator_, random()).WillOnce(Return(60));
  EXPECT_FALSE(loader_->snapshot().featureEnabled("file8", fractional_percent)); // valid data

  // We currently expect that runtime values represented as fractional percents that are provided as
  // integers are parsed simply as percents (denominator of 100).
  EXPECT_CALL(generator_, random()).WillOnce(Return(53));
  EXPECT_FALSE(loader_->snapshot().featureEnabled("file10", fractional_percent)); // valid int data
  EXPECT_CALL(generator_, random()).WillOnce(Return(51));
  EXPECT_TRUE(loader_->snapshot().featureEnabled("file10", fractional_percent)); // valid int data

  // Invalid fractional percent is ignored.
  EXPECT_CALL(generator_, random()).WillOnce(Return(4));
  EXPECT_TRUE(
      loader_->snapshot().featureEnabled("file9", fractional_percent)); // invalid proto data
  EXPECT_CALL(generator_, random()).WillOnce(Return(6));
  EXPECT_FALSE(
      loader_->snapshot().featureEnabled("file9", fractional_percent)); // invalid proto data
  EXPECT_CALL(generator_, random()).WillOnce(Return(4));
  EXPECT_TRUE(
      loader_->snapshot().featureEnabled("false_friend", fractional_percent)); // invalid proto data
  EXPECT_CALL(generator_, random()).WillOnce(Return(6));
  EXPECT_FALSE(
      loader_->snapshot().featureEnabled("false_friend", fractional_percent)); // invalid proto data

  // Numerator only FractionalPercent is handled.
  EXPECT_CALL(generator_, random()).WillOnce(Return(50));
  EXPECT_TRUE(
      loader_->snapshot().featureEnabled("numerator_only", fractional_percent)); // valid data
  EXPECT_CALL(generator_, random()).WillOnce(Return(60));
  EXPECT_FALSE(
      loader_->snapshot().featureEnabled("numerator_only", fractional_percent)); // valid data

  // Denominator only FractionalPercent is handled.
  EXPECT_CALL(generator_, random()).WillOnce(Return(4));
  EXPECT_FALSE(
      loader_->snapshot().featureEnabled("denominator_only", fractional_percent)); // valid data
  EXPECT_CALL(generator_, random()).WillOnce(Return(6));
  EXPECT_FALSE(
      loader_->snapshot().featureEnabled("denominator_only", fractional_percent)); // valid data

  // Empty message is handled.
  EXPECT_CALL(generator_, random()).WillOnce(Return(4));
  EXPECT_FALSE(loader_->snapshot().featureEnabled("empty", fractional_percent)); // valid data
  EXPECT_CALL(generator_, random()).WillOnce(Return(6));
  EXPECT_FALSE(loader_->snapshot().featureEnabled("empty", fractional_percent)); // valid data
}

class DiskLayerTest : public testing::Test {
protected:
  DiskLayerTest() : api_(Api::createApiForTest()) {}

  Api::ApiPtr api_;
};

TEST_F(DiskLayerTest, IllegalPath) {
#ifdef WIN32
  // no illegal paths on Windows at the moment
  return;
#endif
  EXPECT_THROW_WITH_MESSAGE(DiskLayer("test", "/dev", *api_), EnvoyException, "Invalid path: /dev");
}

// Validate that we catch recursion that goes too deep in the runtime filesystem
// walk.
TEST_F(DiskLayerTest, Loop) {
  EXPECT_THROW_WITH_MESSAGE(
      DiskLayer("test", TestEnvironment::temporaryPath("test/common/runtime/test_data/loop"),
                *api_),
      EnvoyException, "Walk recursion depth exceeded 16");
}

TEST(NoRuntime, FeatureEnabled) {
  // Make sure the registry is not set up.
  ASSERT_TRUE(Runtime::LoaderSingleton::getExisting() == nullptr);

  // Feature defaults should still work.
  EXPECT_EQ(false, runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));
  EXPECT_EQ(true, runtimeFeatureEnabled("envoy.reloadable_features.test_feature_true"));
}

} // namespace
} // namespace Runtime
} // namespace Envoy
