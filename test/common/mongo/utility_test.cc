#include "common/mongo/bson_impl.h"
#include "common/mongo/codec_impl.h"
#include "common/mongo/utility.h"

namespace Mongo {

TEST(MongoMessageUtilityTest, QueryType) {
  {
    QueryMessageImpl q(0, 0);
    q.query(Bson::DocumentImpl::create());
    EXPECT_EQ(MessageUtility::QueryType::ScatterGet, MessageUtility::queryType(q));
  }

  {
    QueryMessageImpl q(0, 0);
    q.query(Bson::DocumentImpl::create()->addInt32("_id", 2));
    EXPECT_EQ(MessageUtility::QueryType::PrimaryKey, MessageUtility::queryType(q));
  }

  {
    QueryMessageImpl q(0, 0);
    q.query(Bson::DocumentImpl::create()->addDocument(
        "_id", Bson::DocumentImpl::create()->addArray("$in", Bson::DocumentImpl::create())));
    EXPECT_EQ(MessageUtility::QueryType::MultiGet, MessageUtility::queryType(q));
  }

  {
    QueryMessageImpl q(0, 0);
    q.query(Bson::DocumentImpl::create()->addDocument("$query", Bson::DocumentImpl::create()));
    EXPECT_EQ(MessageUtility::QueryType::ScatterGet, MessageUtility::queryType(q));
  }

  {
    QueryMessageImpl q(0, 0);
    q.query(Bson::DocumentImpl::create()->addDocument(
        "$query", Bson::DocumentImpl::create()->addInt32("_id", 2)));
    EXPECT_EQ(MessageUtility::QueryType::PrimaryKey, MessageUtility::queryType(q));
  }

  {
    QueryMessageImpl q(0, 0);
    q.query(Bson::DocumentImpl::create()->addDocument(
        "$query",
        Bson::DocumentImpl::create()->addDocument(
            "_id", Bson::DocumentImpl::create()->addArray("$in", Bson::DocumentImpl::create()))));
    EXPECT_EQ(MessageUtility::QueryType::MultiGet, MessageUtility::queryType(q));
  }
}

TEST(MongoMessageUtilityTest, CollectionFromFullCollectionName) {
  EXPECT_EQ("foo", MessageUtility::collectionFromFullCollectionName("db.foo"));
  EXPECT_THROW(MessageUtility::collectionFromFullCollectionName("foo"), EnvoyException);
}

TEST(MongoMessageUtilityTest, QueryCallingFunction) {
  {
    QueryMessageImpl q(0, 0);
    q.query(Bson::DocumentImpl::create());
    EXPECT_EQ("", MessageUtility::queryCallingFunction(q));
  }

  {
    QueryMessageImpl q(0, 0);
    q.query(Bson::DocumentImpl::create()->addString("$comment", "bad json"));
    EXPECT_EQ("", MessageUtility::queryCallingFunction(q));
  }

  {
    std::string json = R"EOF(
      {"hostname":"api-production-iad-canary","httpUniqueId":"VqqX7H8AAQEAAE@8EUkAAAAR","callingFunction":"getByMongoId"}
    )EOF";

    QueryMessageImpl q(0, 0);
    q.query(Bson::DocumentImpl::create()->addString("$comment", std::move(json)));
    EXPECT_EQ("getByMongoId", MessageUtility::queryCallingFunction(q));
  }
}

TEST(MongoMessageUtilityTest, QueryCommand) {
  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.$cmd");
    q.query(Bson::DocumentImpl::create()->addString("foo", "bar"));
    EXPECT_EQ("foo", MessageUtility::queryCommand(q));
  }

  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.foo");
    q.query(Bson::DocumentImpl::create()->addString("foo", "bar"));
    EXPECT_EQ("", MessageUtility::queryCommand(q));
  }

  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.$cmd");
    q.query(Bson::DocumentImpl::create());
    EXPECT_THROW(MessageUtility::queryCommand(q), EnvoyException);
  }

  {
    QueryMessageImpl q(0, 0);
    q.fullCollectionName("db.$cmd");
    q.query(Bson::DocumentImpl::create()->addDocument(
        "$query", Bson::DocumentImpl::create()->addInt32("ismaster", 1)));
    EXPECT_EQ("ismaster", MessageUtility::queryCommand(q));
  }
}

} // Mongo
