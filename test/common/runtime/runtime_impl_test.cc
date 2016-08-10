#include "common/runtime/runtime_impl.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/thread_local/mocks.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;

namespace Runtime {

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
  void setup(const std::string& runtime_override_dir) {
    EXPECT_CALL(dispatcher, createFilesystemWatcher_())
        .WillOnce(ReturnNew<NiceMock<Filesystem::MockWatcher>>());

    loader.reset(new LoaderImpl(dispatcher, tls, "test/common/runtime/test_data/current", "envoy",
                                runtime_override_dir, store, generator));
  }

  Event::MockDispatcher dispatcher;
  NiceMock<ThreadLocal::MockInstance> tls;

  Stats::IsolatedStoreImpl store;
  MockRandomGenerator generator;
  std::unique_ptr<LoaderImpl> loader;
};

TEST_F(RuntimeImplTest, All) {
  setup("envoy_override");

  // Basic string getting.
  EXPECT_EQ("world", loader->snapshot().get("file2"));
  EXPECT_EQ("hello\nworld", loader->snapshot().get("subdir.file3"));
  EXPECT_EQ("", loader->snapshot().get("invalid"));

  // Integer getting.
  EXPECT_EQ(1UL, loader->snapshot().getInteger("file1", 1));
  EXPECT_EQ(2UL, loader->snapshot().getInteger("file3", 1));
  EXPECT_EQ(123UL, loader->snapshot().getInteger("file4", 1));

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

TEST_F(RuntimeImplTest, OverrideFolderDoesNotExist) {
  setup("envoy_override_does_not_exist");

  EXPECT_EQ("hello", loader->snapshot().get("file1"));
}

} // Runtime
