#include <string>

#include "source/extensions/filters/network/mongo_proxy/bson_impl.h"
#include "source/extensions/filters/network/mongo_proxy/codec_impl.h"
#include "source/extensions/filters/network/mongo_proxy/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

TEST(QueryMessageInfoTest, FindCommand) {
  std::string json = R"EOF(
    {"hostname":"api-production-iad-canary","httpUniqueId":"VqqX7H8AAQEAAE@8EUkAAAAR","callingFunction":"getByMongoId"}
  )EOF";

  QueryMessageImpl q(0, 0);
  q.fullCollectionName("db.$cmd");
  q.query(Bson::DocumentImpl::create()
              ->addString("find", "foo_collection")
              ->addString("comment", std::move(json))
              ->addDocument("filter", Bson::DocumentImpl::create()->addString("_id", "foo")));
  QueryMessageInfo info(q);
  EXPECT_EQ("", info.command());
  EXPECT_EQ("foo_collection", info.collection());
  EXPECT_EQ("getByMongoId", info.callsite());
  EXPECT_EQ(QueryMessageInfo::QueryType::PrimaryKey, info.type());
}

TEST(QueryMessageInfoTest, Type) {
  {
    QueryMessageImpl q(1, 0);
    q.fullCollectionName("db.foo");
    q.query(Bson::DocumentImpl::create());
    QueryMessageInfo info(q);
    EXPECT_EQ(QueryMessageInfo::QueryType::ScatterGet, info.type());
    EXPECT_EQ(1, info.requestId());
  }

  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.foo");
    q.query(Bson::DocumentImpl::create()->addInt32("_id", 2));
    QueryMessageInfo info(q);
    EXPECT_EQ(QueryMessageInfo::QueryType::PrimaryKey, info.type());
  }

  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.foo");
    q.query(Bson::DocumentImpl::create()->addDocument(
        "_id", Bson::DocumentImpl::create()->addArray("$in", Bson::DocumentImpl::create())));
    QueryMessageInfo info(q);
    EXPECT_EQ(QueryMessageInfo::QueryType::MultiGet, info.type());
  }

  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.foo");
    q.query(Bson::DocumentImpl::create()->addDocument("$query", Bson::DocumentImpl::create()));
    QueryMessageInfo info(q);
    EXPECT_EQ(QueryMessageInfo::QueryType::ScatterGet, info.type());
  }

  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.foo");
    q.query(Bson::DocumentImpl::create()->addDocument(
        "$query", Bson::DocumentImpl::create()->addInt32("_id", 2)));
    QueryMessageInfo info(q);
    EXPECT_EQ(QueryMessageInfo::QueryType::PrimaryKey, info.type());
  }

  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.foo");
    q.query(Bson::DocumentImpl::create()->addDocument(
        "$query",
        Bson::DocumentImpl::create()->addDocument(
            "_id", Bson::DocumentImpl::create()->addArray("$in", Bson::DocumentImpl::create()))));
    QueryMessageInfo info(q);
    EXPECT_EQ(QueryMessageInfo::QueryType::MultiGet, info.type());
  }
}

TEST(QueryMessageInfoTest, CollectionFromFullCollectionName) {
  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.foo");
    q.query(Bson::DocumentImpl::create());
    QueryMessageInfo info(q);
    EXPECT_EQ("foo", info.collection());
  }

  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("foo");
    EXPECT_THROW((QueryMessageInfo(q)), EnvoyException);
  }
}

TEST(QueryMessageInfoTest, Callsite) {
  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.foo");
    q.query(Bson::DocumentImpl::create());
    QueryMessageInfo info(q);
    EXPECT_EQ("", info.callsite());
  }

  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.foo");
    q.query(Bson::DocumentImpl::create()->addString("$comment", "bad json"));
    QueryMessageInfo info(q);
    EXPECT_EQ("", info.callsite());
  }

  {
    std::string json = R"EOF(
      {"hostname":"api-production-iad-canary","httpUniqueId":"VqqX7H8AAQEAAE@8EUkAAAAR","callingFunction":"getByMongoId"}
    )EOF";

    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.foo");
    q.query(Bson::DocumentImpl::create()->addString("$comment", std::move(json)));
    QueryMessageInfo info(q);
    EXPECT_EQ("getByMongoId", info.callsite());
  }
}

TEST(QueryMessageInfoTest, MaxTime) {
  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.foo");
    q.query(Bson::DocumentImpl::create());
    QueryMessageInfo info(q);
    EXPECT_EQ(0, info.maxTime());
  }

  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.foo");
    q.query(Bson::DocumentImpl::create()->addInt32("$maxTimeMS", 1212));
    QueryMessageInfo info(q);
    EXPECT_EQ(1212, info.maxTime());
  }

  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.foo");
    q.query(Bson::DocumentImpl::create()->addInt64("$maxTimeMS", 1212));
    QueryMessageInfo info(q);
    EXPECT_EQ(1212, info.maxTime());
  }

  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.foo");
    q.query(Bson::DocumentImpl::create()->addInt64("maxTimeMS", 2400));
    QueryMessageInfo info(q);
    EXPECT_EQ(2400, info.maxTime());
  }
}

TEST(QueryMessageInfoTest, Command) {
  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.$cmd");
    q.query(Bson::DocumentImpl::create()->addString("foo", "bar"));
    QueryMessageInfo info(q);
    EXPECT_EQ("foo", info.command());
  }

  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.foo");
    q.query(Bson::DocumentImpl::create()->addString("foo", "bar"));
    QueryMessageInfo info(q);
    EXPECT_EQ("", info.command());
  }

  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.$cmd");
    q.query(Bson::DocumentImpl::create());
    EXPECT_THROW((QueryMessageInfo(q)), EnvoyException);
  }

  std::vector<std::pair<std::string, std::string>> test_cases = {
      {"collstats", "collStats"},         {"dbstats", "dbStats"},
      {"findandmodify", "findAndModify"}, {"getlasterror", "getLastError"},
      {"ismaster", "isMaster"},
  };
  for (const auto& test : test_cases) {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.$cmd");
    q.query(Bson::DocumentImpl::create()->addDocument(
        "$query", Bson::DocumentImpl::create()->addInt32(test.first, 1)));
    QueryMessageInfo info(q);
    EXPECT_EQ(test.second, info.command());
  }
}

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
