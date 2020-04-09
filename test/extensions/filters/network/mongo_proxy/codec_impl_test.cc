#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/json/json_loader.h"

#include "extensions/filters/network/mongo_proxy/bson_impl.h"
#include "extensions/filters/network/mongo_proxy/codec_impl.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;
using testing::NiceMock;
using testing::Pointee;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

class TestDecoderCallbacks : public DecoderCallbacks {
public:
  void decodeGetMore(GetMoreMessagePtr&& message) override { decodeGetMore_(message); }
  void decodeInsert(InsertMessagePtr&& message) override { decodeInsert_(message); }
  void decodeKillCursors(KillCursorsMessagePtr&& message) override { decodeKillCursors_(message); }
  void decodeQuery(QueryMessagePtr&& message) override { decodeQuery_(message); }
  void decodeReply(ReplyMessagePtr&& message) override { decodeReply_(message); }
  void decodeCommand(CommandMessagePtr&& message) override { decodeCommand_(message); }
  void decodeCommandReply(CommandReplyMessagePtr&& message) override {
    decodeCommandReply_(message);
  }

  MOCK_METHOD(void, decodeGetMore_, (GetMoreMessagePtr & message));
  MOCK_METHOD(void, decodeInsert_, (InsertMessagePtr & message));
  MOCK_METHOD(void, decodeKillCursors_, (KillCursorsMessagePtr & message));
  MOCK_METHOD(void, decodeQuery_, (QueryMessagePtr & message));
  MOCK_METHOD(void, decodeReply_, (ReplyMessagePtr & message));
  MOCK_METHOD(void, decodeCommand_, (CommandMessagePtr & message));
  MOCK_METHOD(void, decodeCommandReply_, (CommandReplyMessagePtr & message));
};

class MongoCodecImplTest : public testing::Test {
public:
  Buffer::OwnedImpl output_;
  EncoderImpl encoder_{output_};
  NiceMock<TestDecoderCallbacks> callbacks_;
  DecoderImpl decoder_{callbacks_};
};

TEST_F(MongoCodecImplTest, QueryEqual) {
  {
    QueryMessageImpl q1(0, 0);
    QueryMessageImpl q2(1, 1);
    EXPECT_FALSE(q1 == q2);
  }

  {
    QueryMessageImpl q1(0, 0);
    q1.fullCollectionName("hello");
    QueryMessageImpl q2(0, 0);
    q2.fullCollectionName("world");
    EXPECT_FALSE(q1 == q2);
  }

  {
    QueryMessageImpl q1(0, 0);
    q1.query(Bson::DocumentImpl::create()->addString("hello", "world"));
    QueryMessageImpl q2(0, 0);
    q2.query(Bson::DocumentImpl::create()->addString("world", "hello"));
    EXPECT_FALSE(q1 == q2);
  }

  {
    QueryMessageImpl q1(0, 0);
    q1.returnFieldsSelector(Bson::DocumentImpl::create()->addString("hello", "world"));
    QueryMessageImpl q2(0, 0);
    q2.returnFieldsSelector(Bson::DocumentImpl::create()->addString("world", "hello"));
    EXPECT_FALSE(q1 == q2);
  }
}

TEST_F(MongoCodecImplTest, Query) {
  QueryMessageImpl query(1, 1);
  query.flags(0x4);
  query.fullCollectionName("test");
  query.numberToSkip(20);
  query.numberToReturn(-1);
  query.query(
      Bson::DocumentImpl::create()
          ->addString("string", "string")
          ->addSymbol("symbol", "symbol")
          ->addDouble("double", 2.1)
          ->addDocument("document", Bson::DocumentImpl::create()->addString("hello", "world"))
          ->addArray("array", Bson::DocumentImpl::create()->addString("0", "foo"))
          ->addBinary("binary", "binary_value")
          ->addObjectId("object_id", Bson::Field::ObjectId())
          ->addBoolean("true", true)
          ->addBoolean("false", false)
          ->addDatetime("datetime", 1)
          ->addNull("null")
          ->addRegex("regex", {"hello", ""})
          ->addInt32("int32", 1)
          ->addTimestamp("timestamp", 1000)
          ->addInt64("int64", 2));

  QueryMessageImpl query2(2, 2);
  query2.fullCollectionName("test2");
  query2.query(Bson::DocumentImpl::create()->addString("string2", "string2_value"));
  query2.returnFieldsSelector(Bson::DocumentImpl::create()->addDouble("double2", -2.3));

  Json::Factory::loadFromString(query.toString(true));
  EXPECT_NO_THROW(Json::Factory::loadFromString(query.toString(true)));
  EXPECT_NO_THROW(Json::Factory::loadFromString(query.toString(false)));
  EXPECT_NO_THROW(Json::Factory::loadFromString(query2.toString(true)));
  EXPECT_NO_THROW(Json::Factory::loadFromString(query2.toString(false)));

  encoder_.encodeQuery(query);
  encoder_.encodeQuery(query2);
  EXPECT_CALL(callbacks_, decodeQuery_(Pointee(Eq(query))));
  EXPECT_CALL(callbacks_, decodeQuery_(Pointee(Eq(query2))));
  decoder_.onData(output_);
}

TEST_F(MongoCodecImplTest, ReplyEqual) {
  {
    ReplyMessageImpl r1(0, 0);
    ReplyMessageImpl r2(1, 1);
    EXPECT_FALSE(r1 == r2);
  }

  {
    ReplyMessageImpl r1(0, 0);
    r1.cursorId(1);
    ReplyMessageImpl r2(0, 0);
    r2.cursorId(2);
    EXPECT_FALSE(r1 == r2);
  }

  {
    ReplyMessageImpl r1(0, 0);
    r1.numberReturned(1);
    r1.documents().push_back(Bson::DocumentImpl::create()->addString("hello", "world"));
    ReplyMessageImpl r2(0, 0);
    r2.numberReturned(1);
    r2.documents().push_back(Bson::DocumentImpl::create()->addString("world", "hello"));
    EXPECT_FALSE(r1 == r2);
  }
}

TEST_F(MongoCodecImplTest, Reply) {
  ReplyMessageImpl reply(2, 2);
  reply.flags(0x8);
  reply.cursorId(20000);
  reply.startingFrom(20);
  reply.numberReturned(2);
  reply.documents().push_back(Bson::DocumentImpl::create());
  reply.documents().push_back(Bson::DocumentImpl::create());

  EXPECT_NO_THROW(Json::Factory::loadFromString(reply.toString(true)));
  EXPECT_NO_THROW(Json::Factory::loadFromString(reply.toString(false)));

  encoder_.encodeReply(reply);
  EXPECT_CALL(callbacks_, decodeReply_(Pointee(Eq(reply))));
  decoder_.onData(output_);
}

TEST_F(MongoCodecImplTest, GetMoreEqual) {
  {
    GetMoreMessageImpl g1(0, 0);
    GetMoreMessageImpl g2(1, 1);
    EXPECT_FALSE(g1 == g2);
  }

  {
    GetMoreMessageImpl g1(0, 0);
    g1.cursorId(1);
    GetMoreMessageImpl g2(0, 0);
    g1.cursorId(2);
    EXPECT_FALSE(g1 == g2);
  }
}

TEST_F(MongoCodecImplTest, GetMore) {
  GetMoreMessageImpl get_more(3, 3);
  get_more.fullCollectionName("test");
  get_more.numberToReturn(20);
  get_more.cursorId(20000);

  EXPECT_NO_THROW(Json::Factory::loadFromString(get_more.toString(true)));
  EXPECT_NO_THROW(Json::Factory::loadFromString(get_more.toString(false)));

  encoder_.encodeGetMore(get_more);
  EXPECT_CALL(callbacks_, decodeGetMore_(Pointee(Eq(get_more))));
  decoder_.onData(output_);
}

TEST_F(MongoCodecImplTest, InsertEqual) {
  {
    InsertMessageImpl i1(0, 0);
    InsertMessageImpl i2(1, 1);
    EXPECT_FALSE(i1 == i2);
  }

  {
    InsertMessageImpl i1(0, 0);
    i1.fullCollectionName("hello");
    InsertMessageImpl i2(0, 0);
    i2.fullCollectionName("world");
    EXPECT_FALSE(i1 == i2);
  }

  {
    InsertMessageImpl i1(0, 0);
    i1.fullCollectionName("hello");
    i1.documents().push_back(Bson::DocumentImpl::create()->addString("hello", "world"));
    InsertMessageImpl i2(0, 0);
    i2.fullCollectionName("hello");
    i2.documents().push_back(Bson::DocumentImpl::create()->addString("world", "hello"));
    EXPECT_FALSE(i1 == i2);
  }
}

TEST_F(MongoCodecImplTest, Insert) {
  InsertMessageImpl insert(4, 4);
  insert.flags(0x8);
  insert.fullCollectionName("test");
  insert.documents().push_back(Bson::DocumentImpl::create());
  insert.documents().push_back(Bson::DocumentImpl::create());

  EXPECT_NO_THROW(Json::Factory::loadFromString(insert.toString(true)));
  EXPECT_NO_THROW(Json::Factory::loadFromString(insert.toString(false)));

  encoder_.encodeInsert(insert);
  EXPECT_CALL(callbacks_, decodeInsert_(Pointee(Eq(insert))));
  decoder_.onData(output_);
}

TEST_F(MongoCodecImplTest, KillCursorsEqual) {
  {
    KillCursorsMessageImpl k1(0, 0);
    KillCursorsMessageImpl k2(1, 1);
    EXPECT_FALSE(k1 == k2);
  }

  {
    KillCursorsMessageImpl k1(0, 0);
    k1.numberOfCursorIds(1);
    KillCursorsMessageImpl k2(0, 0);
    k2.numberOfCursorIds(2);
    EXPECT_FALSE(k1 == k2);
  }

  {
    KillCursorsMessageImpl k1(0, 0);
    k1.numberOfCursorIds(1);
    k1.cursorIds({1});
    KillCursorsMessageImpl k2(0, 0);
    k2.numberOfCursorIds(1);
    k2.cursorIds({2});
    EXPECT_FALSE(k1 == k2);
  }
}

TEST_F(MongoCodecImplTest, KillCursors) {
  KillCursorsMessageImpl kill(5, 5);
  kill.numberOfCursorIds(2);
  kill.cursorIds({20000, 40000});

  EXPECT_NO_THROW(Json::Factory::loadFromString(kill.toString(true)));
  EXPECT_NO_THROW(Json::Factory::loadFromString(kill.toString(false)));

  encoder_.encodeKillCursors(kill);
  EXPECT_CALL(callbacks_, decodeKillCursors_(Pointee(Eq(kill))));
  decoder_.onData(output_);
}

TEST_F(MongoCodecImplTest, EncodeExceptions) {
  QueryMessageImpl q(0, 0);
  EXPECT_THROW(encoder_.encodeQuery(q), EnvoyException);
  q.fullCollectionName("hello");
  EXPECT_THROW(encoder_.encodeQuery(q), EnvoyException);
  q.query(Bson::DocumentImpl::create());
  encoder_.encodeQuery(q);

  KillCursorsMessageImpl k(0, 0);
  EXPECT_THROW(encoder_.encodeKillCursors(k), EnvoyException);
  k.numberOfCursorIds(1);
  EXPECT_THROW(encoder_.encodeKillCursors(k), EnvoyException);
  k.cursorIds({1});
  encoder_.encodeKillCursors(k);

  InsertMessageImpl i(0, 0);
  EXPECT_THROW(encoder_.encodeInsert(i), EnvoyException);
  i.fullCollectionName("hello");
  EXPECT_THROW(encoder_.encodeInsert(i), EnvoyException);
  i.documents().push_back(Bson::DocumentImpl::create());
  encoder_.encodeInsert(i);

  GetMoreMessageImpl g(0, 0);
  EXPECT_THROW(encoder_.encodeGetMore(g), EnvoyException);
  g.fullCollectionName("hello");
  EXPECT_THROW(encoder_.encodeGetMore(g), EnvoyException);
  g.cursorId(1);
  encoder_.encodeGetMore(g);
}

TEST_F(MongoCodecImplTest, PartialMessages) {
  output_.add("2");
  decoder_.onData(output_);
  output_.drain(output_.length());

  Bson::BufferHelper::writeInt32(output_, 100);
  decoder_.onData(output_);
  EXPECT_EQ(4U, output_.length());
}

TEST_F(MongoCodecImplTest, InvalidMessage) {
  Bson::BufferHelper::writeInt32(output_, 16); // Size
  Bson::BufferHelper::writeInt32(output_, 0);  // Request ID
  Bson::BufferHelper::writeInt32(output_, 1);  // Response to
  Bson::BufferHelper::writeInt32(output_, 2);  // Invalid op
  EXPECT_THROW(decoder_.onData(output_), EnvoyException);
}

TEST_F(MongoCodecImplTest, QueryToStringWithEscape) {
  QueryMessageImpl query(1, 1);
  query.flags(0x4);
  query.fullCollectionName("test");
  query.numberToSkip(20);
  query.numberToReturn(-1);
  query.query(Bson::DocumentImpl::create()->addString("string_need_esc", "{\"foo\": \"bar\n\"}"));

  EXPECT_EQ(R"EOF({"opcode": "OP_QUERY", "id": 1, "response_to": 1, "flags": "0x4", )EOF"
            R"EOF("collection": "test", "skip": 20, "return": -1, "query": )EOF"
            R"EOF({"string_need_esc": "{\"foo\": \"bar\n\"}"}, "fields": {}})EOF",
            query.toString(true));

  EXPECT_NO_THROW(Json::Factory::loadFromString(query.toString(true)));
  EXPECT_NO_THROW(Json::Factory::loadFromString(query.toString(false)));
}

TEST_F(MongoCodecImplTest, CommandEqual) {
  {
    CommandMessageImpl m1(0, 0);
    CommandMessageImpl m2(1, 1);
    EXPECT_FALSE(m1 == m2);
  }

  // Trigger fail on comparing metadata.
  {
    CommandMessageImpl m1(0, 0);
    m1.metadata(Bson::DocumentImpl::create()->addString("hello", "world"));
    CommandMessageImpl m2(1, 1);
    m2.metadata(Bson::DocumentImpl::create()->addString("world", "hello"));
    EXPECT_FALSE(m1 == m2);
  }

  // Trigger fail on comparing commandArgs.
  {
    CommandMessageImpl m1(0, 0);
    m1.commandArgs(Bson::DocumentImpl::create()->addString("hello", "world"));
    CommandMessageImpl m2(1, 1);
    m2.commandArgs(Bson::DocumentImpl::create()->addString("world", "hello"));
    EXPECT_FALSE(m1 == m2);
  }

  // Trigger fail on comparing inputDocs.
  {
    CommandMessageImpl m1(0, 0);
    m1.inputDocs().push_back(Bson::DocumentImpl::create()->addString("hello", "world"));
    CommandMessageImpl m2(1, 1);
    m2.inputDocs().push_back(Bson::DocumentImpl::create()->addString("world", "hello"));
    EXPECT_FALSE(m1 == m2);
  }
}

TEST_F(MongoCodecImplTest, Command) {
  CommandMessageImpl command(15, 25);

  command.database(std::string("Test database"));
  command.commandName(std::string("Test command name"));
  command.metadata(Bson::DocumentImpl::create());
  command.commandArgs(Bson::DocumentImpl::create());
  command.inputDocs().push_back(Bson::DocumentImpl::create()->addString("world", "hello"));

  EXPECT_NO_THROW(Json::Factory::loadFromString(command.toString(true)));
  EXPECT_NO_THROW(Json::Factory::loadFromString(command.toString(false)));

  encoder_.encodeCommand(command);
  EXPECT_CALL(callbacks_, decodeCommand_(Pointee(Eq(command))));
  decoder_.onData(output_);
}

TEST_F(MongoCodecImplTest, CommandReplyEqual) {
  {
    CommandReplyMessageImpl m1(0, 0);
    CommandReplyMessageImpl m2(1, 1);
    EXPECT_FALSE(m1 == m2);
  }

  // Trigger fail on comparing metadata.
  {
    CommandReplyMessageImpl m1(0, 0);
    m1.metadata(Bson::DocumentImpl::create()->addString("hello", "world"));
    CommandReplyMessageImpl m2(1, 1);
    m2.metadata(Bson::DocumentImpl::create()->addString("world", "hello"));
    EXPECT_FALSE(m1 == m2);
  }

  // Trigger fail on comparing commandReply.
  {
    CommandReplyMessageImpl m1(0, 0);
    m1.commandReply(Bson::DocumentImpl::create()->addString("hello", "world"));
    CommandReplyMessageImpl m2(1, 1);
    m2.commandReply(Bson::DocumentImpl::create()->addString("world", "hello"));
    EXPECT_FALSE(m1 == m2);
  }

  // Trigger fail on comparing outputDocs.
  {
    CommandReplyMessageImpl m1(0, 0);
    m1.outputDocs().push_back(Bson::DocumentImpl::create()->addString("hello", "world"));
    CommandReplyMessageImpl m2(1, 1);
    m2.outputDocs().push_back(Bson::DocumentImpl::create()->addString("world", "hello"));
    EXPECT_FALSE(m1 == m2);
  }
}
TEST_F(MongoCodecImplTest, CommandReply) {
  CommandReplyMessageImpl commandReply(16, 26);

  commandReply.metadata(Bson::DocumentImpl::create());
  commandReply.commandReply(Bson::DocumentImpl::create());
  commandReply.outputDocs().push_back(Bson::DocumentImpl::create()->addString("world", "hello"));

  EXPECT_NO_THROW(Json::Factory::loadFromString(commandReply.toString(true)));
  EXPECT_NO_THROW(Json::Factory::loadFromString(commandReply.toString(false)));

  encoder_.encodeCommandReply(commandReply);
  EXPECT_CALL(callbacks_, decodeCommandReply_(Pointee(Eq(commandReply))));
  decoder_.onData(output_);
}

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
