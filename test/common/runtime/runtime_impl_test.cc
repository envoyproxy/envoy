#include <memory>
#include <string>

#include "common/config/runtime_utility.h"
#include "common/runtime/runtime_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;

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

class LoaderImplTest : public testing::Test {
protected:
  LoaderImplTest() : api_(Api::createApiForTest(store_)) { local_info_.node_.set_cluster(""); }

  virtual void setup() {
    EXPECT_CALL(dispatcher_, createFilesystemWatcher_()).WillRepeatedly(InvokeWithoutArgs([this] {
      Filesystem::MockWatcher* mock_watcher = new NiceMock<Filesystem::MockWatcher>();
      EXPECT_CALL(*mock_watcher, addWatch(_, Filesystem::Watcher::Events::MovedTo, _))
          .WillRepeatedly(Invoke(
              [this](const std::string& path, uint32_t, Filesystem::Watcher::OnChangedCb cb) {
                EXPECT_EQ(path, expected_watch_root_);
                on_changed_cbs_.emplace_back(cb);
              }));
      return mock_watcher;
    }));
  }

  Event::MockDispatcher dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::IsolatedStoreImpl store_;
  MockRandomGenerator generator_;
  std::unique_ptr<LoaderImpl> loader_;
  Api::ApiPtr api_;
  Upstream::MockClusterManager cm_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Init::MockManager init_manager_;
  std::vector<Filesystem::Watcher::OnChangedCb> on_changed_cbs_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  std::string expected_watch_root_;
};

class DiskLoaderImplTest : public LoaderImplTest {
public:
  void SetUp() override {
    TestEnvironment::exec(
        {TestEnvironment::runfilesPath("test/common/runtime/filesystem_setup.sh")});
  }

  void TearDown() override {
    TestEnvironment::removePath(TestEnvironment::temporaryPath("test/common/runtime/test_data"));
  }

  void run(const std::string& primary_dir, const std::string& override_dir) {
    envoy::config::bootstrap::v2::Runtime runtime;
    runtime.mutable_base()->MergeFrom(base_);
    expected_watch_root_ = TestEnvironment::temporaryPath(primary_dir);
    runtime.set_symlink_root(expected_watch_root_);
    runtime.set_subdirectory("envoy");
    runtime.set_override_subdirectory(override_dir);

    envoy::config::bootstrap::v2::LayeredRuntime layered_runtime;
    Config::translateRuntime(runtime, layered_runtime);
    loader_ =
        std::make_unique<LoaderImpl>(dispatcher_, tls_, layered_runtime, local_info_, init_manager_,
                                     store_, generator_, validation_visitor_, *api_);
  }

  void write(const std::string& path, const std::string& value) {
    TestEnvironment::writeStringToFileForTest(path, value);
  }

  void updateDiskLayer(uint32_t layer) {
    ASSERT_LT(layer, on_changed_cbs_.size());
    on_changed_cbs_[layer](Filesystem::Watcher::Events::MovedTo);
  }

  ProtobufWkt::Struct base_;
};

TEST_F(DiskLoaderImplTest, DoubleUintInteraction) {
  setup();
  run("test/common/runtime/test_data/current", "envoy_override");

  EXPECT_EQ(2UL, loader_->snapshot().getInteger("file3", 1));
  EXPECT_EQ(2.0, loader_->snapshot().getDouble("file3", 1.1));
}

TEST_F(DiskLoaderImplTest, DoubleUintInteractionNegatives) {
  setup();
  run("test/common/runtime/test_data/current", "envoy_override");

  EXPECT_EQ(1, loader_->snapshot().getInteger("file_with_negative_double", 1));
  EXPECT_EQ(-4.2, loader_->snapshot().getDouble("file_with_negative_double", 1.1));
}

TEST_F(DiskLoaderImplTest, All) {
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

  // Double getting.
  // Bogus string, expect default.
  EXPECT_EQ(42.1, loader_->snapshot().getDouble("file_with_words", 42.1));
  // Valid float string.
  EXPECT_EQ(23.2, loader_->snapshot().getDouble("file_with_double", 1.1));
  // Valid float string followed by newlines.
  EXPECT_EQ(3.141, loader_->snapshot().getDouble("file_with_double_newlines", 1.1));

  const auto snapshot = reinterpret_cast<const SnapshotImpl*>(&loader_->snapshot());

  // Validate that the layer name is set properly for static layers.
  EXPECT_EQ("base", snapshot->getLayers()[0]->name());
  EXPECT_EQ("root", snapshot->getLayers()[1]->name());
  EXPECT_EQ("override", snapshot->getLayers()[2]->name());
  EXPECT_EQ("admin", snapshot->getLayers()[3]->name());

  // Boolean getting.
  // Lower-case boolean specification.
  EXPECT_EQ(true, snapshot->getBoolean("file11", false));
  EXPECT_EQ(true, snapshot->getBoolean("file11", true));
  // Mixed-case boolean specification.
  EXPECT_EQ(false, snapshot->getBoolean("file12", true));
  EXPECT_EQ(false, snapshot->getBoolean("file12", false));
  // Lower-case boolean specification with leading whitespace.
  EXPECT_EQ(true, snapshot->getBoolean("file13", true));
  EXPECT_EQ(true, snapshot->getBoolean("file13", false));
  // File1 is not a boolean. Should take default.
  EXPECT_EQ(true, snapshot->getBoolean("file1", true));
  EXPECT_EQ(false, snapshot->getBoolean("file1", false));

  // Feature defaults.
  // test_feature_true is explicitly set true in runtime_features.cc
  EXPECT_EQ(true, snapshot->runtimeFeatureEnabled("envoy.reloadable_features.test_feature_true"));
  // test_feature_false is not in runtime_features.cc and so is false by default.
  EXPECT_EQ(false, snapshot->runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));

  // Deprecation
#ifdef ENVOY_DISABLE_DEPRECATED_FEATURES
  EXPECT_EQ(false, snapshot->deprecatedFeatureEnabled("random_string_should_be_enabled"));
#else
  EXPECT_EQ(true, snapshot->deprecatedFeatureEnabled("random_string_should_be_enabled"));
#endif
  EXPECT_EQ(false, snapshot->deprecatedFeatureEnabled(
                       "envoy.deprecated_features.deprecated.proto:is_deprecated_fatal"));

  // Feature defaults via helper function.
  EXPECT_EQ(false, runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));
  EXPECT_EQ(true, runtimeFeatureEnabled("envoy.reloadable_features.test_feature_true"));

  // Files with comments.
  EXPECT_EQ(123UL, loader_->snapshot().getInteger("file5", 1));
  EXPECT_EQ(2.718, loader_->snapshot().getDouble("file_with_double_comment", 1.1));
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

  EXPECT_EQ(0, store_.counter("runtime.load_error").value());
  EXPECT_EQ(1, store_.counter("runtime.load_success").value());
  EXPECT_EQ(23, store_.gauge("runtime.num_keys", Stats::Gauge::ImportMode::NeverImport).value());
  EXPECT_EQ(4, store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());
}

TEST_F(DiskLoaderImplTest, UintLargeIntegerConversion) {
  setup();
  run("test/common/runtime/test_data/current", "envoy_override");

  EXPECT_EQ(1, loader_->snapshot().getInteger("file_with_large_integer", 1));
}

TEST_F(DiskLoaderImplTest, GetLayers) {
  base_ = TestUtility::parseYaml<ProtobufWkt::Struct>(R"EOF(
    foo: whatevs
  )EOF");
  setup();
  run("test/common/runtime/test_data/current", "envoy_override");
  const auto& layers = loader_->snapshot().getLayers();
  EXPECT_EQ(1, store_.counter("runtime.load_success").value());
  EXPECT_EQ(4, layers.size());
  EXPECT_EQ(4, store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());
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
  EXPECT_EQ(2, store_.counter("runtime.load_success").value());
}

TEST_F(DiskLoaderImplTest, BadDirectory) {
  setup();
  run("/baddir", "/baddir");
  EXPECT_EQ(0, store_.counter("runtime.load_error").value());
  EXPECT_EQ(1, store_.counter("runtime.load_success").value());
  EXPECT_EQ(2, store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());
  EXPECT_EQ(0, store_.counter("runtime.override_dir_exists").value());
  EXPECT_EQ(1, store_.counter("runtime.override_dir_not_exists").value());
}

// Validate that an error in a layer will results in appropriate stats tracking.
TEST_F(DiskLoaderImplTest, DiskLayerFailure) {
  setup();
  // Symlink loopy configuration will result in an error.
  run("test/common/runtime/test_data", "loop");
  EXPECT_EQ(1, store_.counter("runtime.load_error").value());
  EXPECT_EQ(0, store_.counter("runtime.load_success").value());
  EXPECT_EQ(2, store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());
  EXPECT_EQ(0, store_.counter("runtime.override_dir_exists").value());
  EXPECT_EQ(1, store_.counter("runtime.override_dir_not_exists").value());
}

TEST_F(DiskLoaderImplTest, OverrideFolderDoesNotExist) {
  setup();
  run("test/common/runtime/test_data/current", "envoy_override_does_not_exist");

  EXPECT_EQ("hello", loader_->snapshot().get("file1"));
  EXPECT_EQ(0, store_.counter("runtime.load_error").value());
  EXPECT_EQ(1, store_.counter("runtime.load_success").value());
  EXPECT_EQ(3, store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());
  EXPECT_EQ(0, store_.counter("runtime.override_dir_exists").value());
  EXPECT_EQ(1, store_.counter("runtime.override_dir_not_exists").value());
}

TEST_F(DiskLoaderImplTest, PercentHandling) {
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
  Stats::Gauge& admin_overrides_active =
      store.gauge("runtime.admin_overrides_active", Stats::Gauge::ImportMode::NeverImport);

  // New string.
  loader.mergeValues({{"foo", "bar"}});
  EXPECT_EQ("bar", loader.snapshot().get("foo"));
  EXPECT_EQ(1, admin_overrides_active.value());

  // Remove new string.
  loader.mergeValues({{"foo", ""}});
  EXPECT_EQ("", loader.snapshot().get("foo"));
  EXPECT_EQ(0, admin_overrides_active.value());

  // New integer.
  loader.mergeValues({{"baz", "42"}});
  EXPECT_EQ(42, loader.snapshot().getInteger("baz", 0));
  EXPECT_EQ(1, admin_overrides_active.value());

  // Remove new integer.
  loader.mergeValues({{"baz", ""}});
  EXPECT_EQ(0, loader.snapshot().getInteger("baz", 0));
  EXPECT_EQ(0, admin_overrides_active.value());

  // New double.
  loader.mergeValues({{"beep", "42.1"}});
  EXPECT_EQ(42.1, loader.snapshot().getDouble("beep", 1.2));
  EXPECT_EQ(1, admin_overrides_active.value());

  // Remove new double.
  loader.mergeValues({{"beep", ""}});
  EXPECT_EQ(1.2, loader.snapshot().getDouble("beep", 1.2));
  EXPECT_EQ(0, admin_overrides_active.value());
}

TEST_F(DiskLoaderImplTest, MergeValues) {
  setup();
  run("test/common/runtime/test_data/current", "envoy_override");
  testNewOverrides(*loader_, store_);
  Stats::Gauge& admin_overrides_active =
      store_.gauge("runtime.admin_overrides_active", Stats::Gauge::ImportMode::NeverImport);

  // Override string
  loader_->mergeValues({{"file2", "new world"}});
  EXPECT_EQ("new world", loader_->snapshot().get("file2"));
  EXPECT_EQ(1, admin_overrides_active.value());

  // Remove overridden string
  loader_->mergeValues({{"file2", ""}});
  EXPECT_EQ("world", loader_->snapshot().get("file2"));
  EXPECT_EQ(0, admin_overrides_active.value());

  // Override integer
  loader_->mergeValues({{"file3", "42"}});
  EXPECT_EQ(42, loader_->snapshot().getInteger("file3", 1));
  EXPECT_EQ(1, admin_overrides_active.value());

  // Remove overridden integer
  loader_->mergeValues({{"file3", ""}});
  EXPECT_EQ(2, loader_->snapshot().getInteger("file3", 1));
  EXPECT_EQ(0, admin_overrides_active.value());

  // Override double
  loader_->mergeValues({{"file_with_double", "42.1"}});
  EXPECT_EQ(42.1, loader_->snapshot().getDouble("file_with_double", 1.1));
  EXPECT_EQ(1, admin_overrides_active.value());

  // Remove overridden double
  loader_->mergeValues({{"file_with_double", ""}});
  EXPECT_EQ(23.2, loader_->snapshot().getDouble("file_with_double", 1.1));
  EXPECT_EQ(0, admin_overrides_active.value());

  // Override override string
  loader_->mergeValues({{"file1", "hello overridden override"}});
  EXPECT_EQ("hello overridden override", loader_->snapshot().get("file1"));
  EXPECT_EQ(1, admin_overrides_active.value());

  // Remove overridden override string
  loader_->mergeValues({{"file1", ""}});
  EXPECT_EQ("hello override", loader_->snapshot().get("file1"));
  EXPECT_EQ(0, admin_overrides_active.value());
  EXPECT_EQ(0, store_.gauge("runtime.admin_overrides_active", Stats::Gauge::ImportMode::NeverImport)
                   .value());

  EXPECT_EQ(15, store_.counter("runtime.load_success").value());
  EXPECT_EQ(4, store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());
}

// Validate that admin overrides disk, disk overrides bootstrap.
TEST_F(DiskLoaderImplTest, LayersOverride) {
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
  updateDiskLayer(0);
  EXPECT_EQ("Mega layer cake", loader_->snapshot().get("file14"));
  EXPECT_EQ("Happy cake", loader_->snapshot().get("file15"));
}

// Validate that multiple admin layers leads to a configuration load failure.
TEST_F(DiskLoaderImplTest, MultipleAdminLayersFail) {
  setup();
  envoy::config::bootstrap::v2::LayeredRuntime layered_runtime;
  {
    auto* layer = layered_runtime.add_layers();
    layer->set_name("admin_0");
    layer->mutable_admin_layer();
  }
  {
    auto* layer = layered_runtime.add_layers();
    layer->set_name("admin_1");
    layer->mutable_admin_layer();
  }
  EXPECT_THROW_WITH_MESSAGE(
      std::make_unique<LoaderImpl>(dispatcher_, tls_, layered_runtime, local_info_, init_manager_,
                                   store_, generator_, validation_visitor_, *api_),
      EnvoyException,
      "Too many admin layers specified in LayeredRuntime, at most one may be specified");
}

class StaticLoaderImplTest : public LoaderImplTest {
protected:
  void setup() override {
    LoaderImplTest::setup();
    envoy::config::bootstrap::v2::LayeredRuntime layered_runtime;
    {
      auto* layer = layered_runtime.add_layers();
      layer->set_name("base");
      layer->mutable_static_layer()->MergeFrom(base_);
    }
    {
      auto* layer = layered_runtime.add_layers();
      layer->set_name("admin");
      layer->mutable_admin_layer();
    }
    loader_ =
        std::make_unique<LoaderImpl>(dispatcher_, tls_, layered_runtime, local_info_, init_manager_,
                                     store_, generator_, validation_visitor_, *api_);
  }

  ProtobufWkt::Struct base_;
};

TEST_F(StaticLoaderImplTest, All) {
  setup();
  EXPECT_EQ("", loader_->snapshot().get("foo"));
  EXPECT_EQ(1UL, loader_->snapshot().getInteger("foo", 1));
  EXPECT_EQ(1.1, loader_->snapshot().getDouble("foo", 1.1));
  EXPECT_CALL(generator_, random()).WillOnce(Return(49));
  EXPECT_TRUE(loader_->snapshot().featureEnabled("foo", 50));
  testNewOverrides(*loader_, store_);
}

// Validate proto parsing sanity.
TEST_F(StaticLoaderImplTest, ProtoParsing) {
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
    file_with_words: "some words"
    file_with_double: 23.2
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

  // Double getting.
  EXPECT_EQ(1.1, loader_->snapshot().getDouble("file_with_words", 1.1));
  EXPECT_EQ(23.2, loader_->snapshot().getDouble("file_with_double", 1.1));

  // Boolean getting.
  const auto snapshot = reinterpret_cast<const SnapshotImpl*>(&loader_->snapshot());

  EXPECT_EQ(true, snapshot->getBoolean("file11", true));
  EXPECT_EQ(true, snapshot->getBoolean("file11", false));

  EXPECT_EQ(false, snapshot->getBoolean("file12", true));
  EXPECT_EQ(false, snapshot->getBoolean("file12", false));

  EXPECT_EQ(false, snapshot->getBoolean("file13", true));
  EXPECT_EQ(false, snapshot->getBoolean("file13", false));

  // Not a boolean. Expect the default.
  EXPECT_EQ(true, snapshot->getBoolean("file1", true));
  EXPECT_EQ(false, snapshot->getBoolean("file1", false));
  EXPECT_EQ(true, snapshot->getBoolean("blah.blah", true));
  EXPECT_EQ(false, snapshot->getBoolean("blah.blah", false));

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

  EXPECT_EQ(0, store_.counter("runtime.load_error").value());
  EXPECT_EQ(1, store_.counter("runtime.load_success").value());
  EXPECT_EQ(17, store_.gauge("runtime.num_keys", Stats::Gauge::ImportMode::NeverImport).value());
  EXPECT_EQ(2, store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());
}

TEST_F(StaticLoaderImplTest, RuntimeFromNonWorkerThreads) {
  // Force the thread to be considered a non-worker thread.
  tls_.registered_ = false;
  setup();

  // Set up foo -> bar
  loader_->mergeValues({{"foo", "bar"}});
  EXPECT_EQ("bar", loader_->threadsafeSnapshot()->get("foo"));
  const Snapshot* original_snapshot_pointer = loader_->threadsafeSnapshot().get();

  // Now set up a test thread which verifies foo -> bar
  //
  // Then change foo and make sure the test thread picks up the change.
  bool read_bar = false;
  bool updated_eep = false;
  Thread::MutexBasicLockable mutex;
  Thread::CondVar foo_read;
  Thread::CondVar foo_changed;
  const Snapshot* original_thread_snapshot_pointer = nullptr;
  auto thread = Thread::threadFactoryForTest().createThread([&]() {
    {
      Thread::LockGuard lock(mutex);
      EXPECT_EQ("bar", loader_->threadsafeSnapshot()->get("foo"));
      read_bar = true;
      original_thread_snapshot_pointer = loader_->threadsafeSnapshot().get();
      EXPECT_EQ(original_thread_snapshot_pointer, loader_->threadsafeSnapshot().get());
      foo_read.notifyOne();
    }

    {
      Thread::LockGuard lock(mutex);
      if (!updated_eep) {
        foo_changed.wait(mutex);
      }
      EXPECT_EQ("eep", loader_->threadsafeSnapshot()->get("foo"));
    }
  });

  {
    Thread::LockGuard lock(mutex);
    if (!read_bar) {
      foo_read.wait(mutex);
    }
    loader_->mergeValues({{"foo", "eep"}});
    updated_eep = true;
  }

  {
    Thread::LockGuard lock(mutex);
    foo_changed.notifyOne();
    EXPECT_EQ("eep", loader_->threadsafeSnapshot()->get("foo"));
  }

  thread->join();
  EXPECT_EQ(original_thread_snapshot_pointer, original_snapshot_pointer);
}

class DiskLayerTest : public testing::Test {
protected:
  DiskLayerTest() : api_(Api::createApiForTest()) {}

  static void SetUpTestSuite() {
    TestEnvironment::exec(
        {TestEnvironment::runfilesPath("test/common/runtime/filesystem_setup.sh")});
  }

  static void TearDownTestSuite() {
    TestEnvironment::removePath(TestEnvironment::temporaryPath("test/common/runtime/test_data"));
  }

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

TEST(NoRuntime, DefaultIntValues) {
  // Make sure the registry is not set up.
  ASSERT_TRUE(Runtime::LoaderSingleton::getExisting() == nullptr);

  // Feature defaults should still work.
  EXPECT_EQ(0x1230000ABCDULL,
            getInteger("envoy.reloadable_features.test_int_feature_default", 0x1230000ABCDULL));
  EXPECT_EQ(0, getInteger("envoy.reloadable_features.test_int_feature_zero", 0));
}

// Test RTDS layer(s).
class RtdsLoaderImplTest : public LoaderImplTest {
public:
  void setup() override {
    LoaderImplTest::setup();

    envoy::config::bootstrap::v2::LayeredRuntime config;
    *config.add_layers()->mutable_static_layer() =
        TestUtility::parseYaml<ProtobufWkt::Struct>(R"EOF(
    foo: whatevs
    bar: yar
  )EOF");
    for (const auto& layer_resource_name : layers_) {
      auto* layer = config.add_layers();
      layer->set_name(layer_resource_name);
      auto* rtds_layer = layer->mutable_rtds_layer();
      rtds_layer->set_name(layer_resource_name);
      rtds_layer->mutable_rtds_config();
    }
    EXPECT_CALL(cm_, subscriptionFactory()).Times(layers_.size());
    EXPECT_CALL(init_manager_, add(_)).WillRepeatedly(Invoke([this](const Init::Target& target) {
      init_target_handles_.emplace_back(target.createHandle("test"));
    }));
    ON_CALL(cm_.subscription_factory_, subscriptionFromConfigSource(_, _, _, _))
        .WillByDefault(testing::Invoke(
            [this](const envoy::api::v2::core::ConfigSource&, absl::string_view, Stats::Scope&,
                   Config::SubscriptionCallbacks& callbacks) -> Config::SubscriptionPtr {
              auto ret = std::make_unique<testing::NiceMock<Config::MockSubscription>>();
              rtds_subscriptions_.push_back(ret.get());
              rtds_callbacks_.push_back(&callbacks);
              return ret;
            }));
    loader_ = std::make_unique<LoaderImpl>(dispatcher_, tls_, config, local_info_, init_manager_,
                                           store_, generator_, validation_visitor_, *api_);
    loader_->initialize(cm_);
    for (auto* sub : rtds_subscriptions_) {
      EXPECT_CALL(*sub, start(_));
    }
    for (auto& handle : init_target_handles_) {
      handle->initialize(init_watcher_);
    }

    // Validate that the layer name is set properly for dynamic layers.
    EXPECT_EQ(layers_[0], loader_->snapshot().getLayers()[1]->name());

    EXPECT_EQ("whatevs", loader_->snapshot().get("foo"));
    EXPECT_EQ("yar", loader_->snapshot().get("bar"));
    EXPECT_EQ("", loader_->snapshot().get("baz"));

    EXPECT_EQ(0, store_.counter("runtime.load_error").value());
    EXPECT_EQ(1, store_.counter("runtime.load_success").value());
    EXPECT_EQ(2, store_.gauge("runtime.num_keys", Stats::Gauge::ImportMode::NeverImport).value());
    EXPECT_EQ(1 + layers_.size(),
              store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());
  }

  void addLayer(absl::string_view name) { layers_.emplace_back(name); }

  void doOnConfigUpdateVerifyNoThrow(const envoy::service::discovery::v2::Runtime& runtime,
                                     uint32_t callback_index = 0) {
    Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
    resources.Add()->PackFrom(runtime);
    VERBOSE_EXPECT_NO_THROW(rtds_callbacks_[callback_index]->onConfigUpdate(resources, ""));
  }

  void doDeltaOnConfigUpdateVerifyNoThrow(const envoy::service::discovery::v2::Runtime& runtime) {
    Protobuf::RepeatedPtrField<envoy::api::v2::Resource> resources;
    auto* resource = resources.Add();
    resource->mutable_resource()->PackFrom(runtime);
    resource->set_version("");
    VERBOSE_EXPECT_NO_THROW(rtds_callbacks_[0]->onConfigUpdate(resources, {}, ""));
  }

  std::vector<std::string> layers_{"some_resource"};
  std::vector<Config::SubscriptionCallbacks*> rtds_callbacks_;
  std::vector<Config::MockSubscription*> rtds_subscriptions_;
  Init::ExpectableWatcherImpl init_watcher_;
  std::vector<Init::TargetHandlePtr> init_target_handles_;
};

// Empty resource lists are rejected.
TEST_F(RtdsLoaderImplTest, UnexpectedSizeEmpty) {
  setup();

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> runtimes;

  EXPECT_CALL(init_watcher_, ready());
  EXPECT_THROW_WITH_MESSAGE(rtds_callbacks_[0]->onConfigUpdate(runtimes, ""), EnvoyException,
                            "Unexpected RTDS resource length: 0");

  EXPECT_EQ(0, store_.counter("runtime.load_error").value());
  EXPECT_EQ(1, store_.counter("runtime.load_success").value());
  EXPECT_EQ(2, store_.gauge("runtime.num_keys", Stats::Gauge::ImportMode::NeverImport).value());
  EXPECT_EQ(2, store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());
}

// > 1 length lists are rejected.
TEST_F(RtdsLoaderImplTest, UnexpectedSizeTooMany) {
  setup();

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> runtimes;
  runtimes.Add();
  runtimes.Add();

  EXPECT_CALL(init_watcher_, ready());
  EXPECT_THROW_WITH_MESSAGE(rtds_callbacks_[0]->onConfigUpdate(runtimes, ""), EnvoyException,
                            "Unexpected RTDS resource length: 2");

  EXPECT_EQ(0, store_.counter("runtime.load_error").value());
  EXPECT_EQ(1, store_.counter("runtime.load_success").value());
  EXPECT_EQ(2, store_.gauge("runtime.num_keys", Stats::Gauge::ImportMode::NeverImport).value());
  EXPECT_EQ(2, store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());
}

// Validate behavior when the config fails delivery at the subscription level.
TEST_F(RtdsLoaderImplTest, FailureSubscription) {
  setup();

  EXPECT_CALL(init_watcher_, ready());
  // onConfigUpdateFailed() should not be called for gRPC stream connection failure
  rtds_callbacks_[0]->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout,
                                           {});

  EXPECT_EQ(0, store_.counter("runtime.load_error").value());
  EXPECT_EQ(1, store_.counter("runtime.load_success").value());
  EXPECT_EQ(2, store_.gauge("runtime.num_keys", Stats::Gauge::ImportMode::NeverImport).value());
  EXPECT_EQ(2, store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());
}

// Unexpected runtime resource name.
TEST_F(RtdsLoaderImplTest, WrongResourceName) {
  setup();

  auto runtime = TestUtility::parseYaml<envoy::service::discovery::v2::Runtime>(R"EOF(
    name: other_resource
    layer:
      foo: bar
      baz: meh
  )EOF");
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  resources.Add()->PackFrom(runtime);
  EXPECT_THROW_WITH_MESSAGE(rtds_callbacks_[0]->onConfigUpdate(resources, ""), EnvoyException,
                            "Unexpected RTDS runtime (expecting some_resource): other_resource");

  EXPECT_EQ("whatevs", loader_->snapshot().get("foo"));
  EXPECT_EQ("yar", loader_->snapshot().get("bar"));
  EXPECT_EQ("", loader_->snapshot().get("baz"));

  EXPECT_EQ(0, store_.counter("runtime.load_error").value());
  EXPECT_EQ(1, store_.counter("runtime.load_success").value());
  EXPECT_EQ(2, store_.gauge("runtime.num_keys", Stats::Gauge::ImportMode::NeverImport).value());
  EXPECT_EQ(2, store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());
}

// Successful update.
TEST_F(RtdsLoaderImplTest, OnConfigUpdateSuccess) {
  setup();

  auto runtime = TestUtility::parseYaml<envoy::service::discovery::v2::Runtime>(R"EOF(
    name: some_resource
    layer:
      foo: bar
      baz: meh
  )EOF");
  EXPECT_CALL(init_watcher_, ready());
  doOnConfigUpdateVerifyNoThrow(runtime);

  EXPECT_EQ("bar", loader_->snapshot().get("foo"));
  EXPECT_EQ("yar", loader_->snapshot().get("bar"));
  EXPECT_EQ("meh", loader_->snapshot().get("baz"));

  EXPECT_EQ(0, store_.counter("runtime.load_error").value());
  EXPECT_EQ(2, store_.counter("runtime.load_success").value());
  EXPECT_EQ(3, store_.gauge("runtime.num_keys", Stats::Gauge::ImportMode::NeverImport).value());
  EXPECT_EQ(2, store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());

  runtime = TestUtility::parseYaml<envoy::service::discovery::v2::Runtime>(R"EOF(
    name: some_resource
    layer:
      baz: saz
  )EOF");
  doOnConfigUpdateVerifyNoThrow(runtime);

  EXPECT_EQ("whatevs", loader_->snapshot().get("foo"));
  EXPECT_EQ("yar", loader_->snapshot().get("bar"));
  EXPECT_EQ("saz", loader_->snapshot().get("baz"));

  EXPECT_EQ(0, store_.counter("runtime.load_error").value());
  EXPECT_EQ(3, store_.counter("runtime.load_success").value());
  EXPECT_EQ(3, store_.gauge("runtime.num_keys", Stats::Gauge::ImportMode::NeverImport).value());
  EXPECT_EQ(2, store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());
}

// Delta style successful update.
TEST_F(RtdsLoaderImplTest, DeltaOnConfigUpdateSuccess) {
  setup();

  auto runtime = TestUtility::parseYaml<envoy::service::discovery::v2::Runtime>(R"EOF(
    name: some_resource
    layer:
      foo: bar
      baz: meh
  )EOF");
  EXPECT_CALL(init_watcher_, ready());
  doDeltaOnConfigUpdateVerifyNoThrow(runtime);

  EXPECT_EQ("bar", loader_->snapshot().get("foo"));
  EXPECT_EQ("yar", loader_->snapshot().get("bar"));
  EXPECT_EQ("meh", loader_->snapshot().get("baz"));

  EXPECT_EQ(0, store_.counter("runtime.load_error").value());
  EXPECT_EQ(2, store_.counter("runtime.load_success").value());
  EXPECT_EQ(3, store_.gauge("runtime.num_keys", Stats::Gauge::ImportMode::NeverImport).value());
  EXPECT_EQ(2, store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());

  runtime = TestUtility::parseYaml<envoy::service::discovery::v2::Runtime>(R"EOF(
    name: some_resource
    layer:
      baz: saz
  )EOF");
  doDeltaOnConfigUpdateVerifyNoThrow(runtime);

  EXPECT_EQ("whatevs", loader_->snapshot().get("foo"));
  EXPECT_EQ("yar", loader_->snapshot().get("bar"));
  EXPECT_EQ("saz", loader_->snapshot().get("baz"));

  EXPECT_EQ(0, store_.counter("runtime.load_error").value());
  EXPECT_EQ(3, store_.counter("runtime.load_success").value());
  EXPECT_EQ(3, store_.gauge("runtime.num_keys", Stats::Gauge::ImportMode::NeverImport).value());
  EXPECT_EQ(2, store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());
}

// Updates with multiple RTDS layers.
TEST_F(RtdsLoaderImplTest, MultipleRtdsLayers) {
  addLayer("another_resource");
  setup();

  EXPECT_EQ("whatevs", loader_->snapshot().get("foo"));
  EXPECT_EQ("yar", loader_->snapshot().get("bar"));
  EXPECT_EQ("", loader_->snapshot().get("baz"));

  auto runtime = TestUtility::parseYaml<envoy::service::discovery::v2::Runtime>(R"EOF(
    name: some_resource
    layer:
      foo: bar
      baz: meh
  )EOF");
  EXPECT_CALL(init_watcher_, ready()).Times(2);
  doOnConfigUpdateVerifyNoThrow(runtime, 0);

  EXPECT_EQ("bar", loader_->snapshot().get("foo"));
  EXPECT_EQ("yar", loader_->snapshot().get("bar"));
  EXPECT_EQ("meh", loader_->snapshot().get("baz"));

  EXPECT_EQ(0, store_.counter("runtime.load_error").value());
  EXPECT_EQ(2, store_.counter("runtime.load_success").value());
  EXPECT_EQ(3, store_.gauge("runtime.num_keys", Stats::Gauge::ImportMode::NeverImport).value());
  EXPECT_EQ(3, store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());

  runtime = TestUtility::parseYaml<envoy::service::discovery::v2::Runtime>(R"EOF(
    name: another_resource
    layer:
      baz: saz
  )EOF");
  doOnConfigUpdateVerifyNoThrow(runtime, 1);

  // Unlike in OnConfigUpdateSuccess, foo latches onto bar as the some_resource
  // layer still applies.
  EXPECT_EQ("bar", loader_->snapshot().get("foo"));
  EXPECT_EQ("yar", loader_->snapshot().get("bar"));
  EXPECT_EQ("saz", loader_->snapshot().get("baz"));

  EXPECT_EQ(0, store_.counter("runtime.load_error").value());
  EXPECT_EQ(3, store_.counter("runtime.load_success").value());
  EXPECT_EQ(3, store_.gauge("runtime.num_keys", Stats::Gauge::ImportMode::NeverImport).value());
  EXPECT_EQ(3, store_.gauge("runtime.num_layers", Stats::Gauge::ImportMode::NeverImport).value());
}

} // namespace
} // namespace Runtime
} // namespace Envoy
