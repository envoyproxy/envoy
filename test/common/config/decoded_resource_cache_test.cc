#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/config/decoded_resource_cache.h"

#include "test/mocks/config/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

namespace Envoy {
namespace Config {
namespace {

Protobuf::Any clusterAny(const std::string& name, uint32_t connect_timeout_s) {
  envoy::config::cluster::v3::Cluster cluster;
  cluster.set_name(name);
  cluster.mutable_connect_timeout()->set_seconds(connect_timeout_s);
  Protobuf::Any any;
  EXPECT_TRUE(any.PackFrom(cluster));
  return any;
}

class DecodedResourceCacheTest : public testing::Test {
protected:
  DecodedResourceCacheTest() {
    ON_CALL(decoder_, decodeResource(_, _))
        .WillByDefault(Invoke([this](const Protobuf::Any& any, Protobuf::Arena& arena)
                                  -> Protobuf::Arena::UniquePtr<Protobuf::Message> {
          ++decodes_;
          auto cluster = Protobuf::Arena::MakeUnique<envoy::config::cluster::v3::Cluster>(&arena);
          EXPECT_TRUE(any.UnpackTo(cluster.get()));
          return cluster;
        }));
    ON_CALL(decoder_, resourceName(_)).WillByDefault(Invoke([](const Protobuf::Message& m) {
      return dynamic_cast<const envoy::config::cluster::v3::Cluster&>(m).name();
    }));
  }

  DecodedResourcePtr decode(const Protobuf::Any& any, const std::string& version) {
    return std::move(cache_.decode(decoder_, any, version, arena_).value());
  }

  testing::NiceMock<MockOpaqueResourceDecoder> decoder_;
  Protobuf::Arena arena_;
  DecodedResourceCache cache_;
  int decodes_{};
};

TEST_F(DecodedResourceCacheTest, UnchangedBytesAreNotDecodedAgain) {
  EXPECT_EQ("a", decode(clusterAny("a", 1), "v1")->name());
  cache_.endUpdate();
  EXPECT_EQ(1, decodes_);

  DecodedResourcePtr again = decode(clusterAny("a", 1), "v2");
  cache_.endUpdate();
  EXPECT_EQ(1, decodes_);
  EXPECT_EQ(1, cache_.hits());
  EXPECT_EQ("a", again->name());
  // The update's version, not the version the bytes were first decoded in.
  EXPECT_EQ("v2", again->version());
  EXPECT_EQ(1, dynamic_cast<const envoy::config::cluster::v3::Cluster&>(again->resource())
                   .connect_timeout()
                   .seconds());
}

TEST_F(DecodedResourceCacheTest, ChangedBytesAreDecoded) {
  decode(clusterAny("a", 1), "v1");
  cache_.endUpdate();
  DecodedResourcePtr changed = decode(clusterAny("a", 2), "v2");
  cache_.endUpdate();
  EXPECT_EQ(2, decodes_);
  EXPECT_EQ(2, dynamic_cast<const envoy::config::cluster::v3::Cluster&>(changed->resource())
                   .connect_timeout()
                   .seconds());
}

TEST_F(DecodedResourceCacheTest, ResourcesMissingFromAnUpdateAreForgotten) {
  decode(clusterAny("a", 1), "v1");
  decode(clusterAny("b", 1), "v1");
  cache_.endUpdate();
  decode(clusterAny("a", 1), "v2"); // b is not in this update.
  cache_.endUpdate();
  decode(clusterAny("b", 1), "v3");
  cache_.endUpdate();
  EXPECT_EQ(3, decodes_);
}

TEST_F(DecodedResourceCacheTest, AbortedUpdateKeepsThePreviousReference) {
  decode(clusterAny("a", 1), "v1");
  cache_.endUpdate();
  decode(clusterAny("c", 1), "v2");
  cache_.abortUpdate();
  // "a" is still reusable after the aborted update.
  decode(clusterAny("a", 1), "v3");
  cache_.endUpdate();
  EXPECT_EQ(2, decodes_);
}

TEST_F(DecodedResourceCacheTest, ReusedResourceOutlivesTheArenaItWasDecodedOn) {
  DecodedResourcePtr reused;
  {
    Protobuf::Arena first_arena;
    ASSERT_TRUE(cache_.decode(decoder_, clusterAny("a", 7), "v1", first_arena).ok());
    cache_.endUpdate();
  }
  reused = decode(clusterAny("a", 7), "v2");
  EXPECT_EQ(7, dynamic_cast<const envoy::config::cluster::v3::Cluster&>(reused->resource())
                   .connect_timeout()
                   .seconds());
}

} // namespace
} // namespace Config
} // namespace Envoy
