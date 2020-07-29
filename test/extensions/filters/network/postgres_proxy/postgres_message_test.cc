#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/postgres_proxy/postgres_message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

// Test basic 2 fields message
TEST(PostgresMessage, DISABLED_Basic) {
  Sequence<> msg;

  auto out = msg.to_string();
  ASSERT_THAT(out, "");
}

TEST(PostgresMessage, SingleField) {
  // Sequence<Int32> msg('B', 5);

  // std::unique_ptr<MessageI> msg = std::make_unique<Sequence<Int32>>('B', 5);
  std::unique_ptr<MessageI> msg = createMsg<Int32>();

  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(12);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[12]");
}

TEST(PostgresMessage, SingleString) {
  std::unique_ptr<MessageI> msg = std::make_unique<Sequence<String>>();

  Buffer::OwnedImpl data;
  data.add("test");
  data.writeBEInt<uint32_t>(0);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[test]");
}

TEST(PostgresMessage, SingleStringViaBind) {
  auto f = std::bind(createMsg<Int32>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(12);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[12]");
}

TEST(PostgresMessage, SingleByte1) {
  auto f = std::bind(createMsg<Byte1>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;
  data.writeBEInt<uint8_t>('S');
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[S]");
}

TEST(PostgresMessage, SingleByteN) {
  auto f = std::bind(createMsg<ByteN>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint8_t>(1);
  data.writeBEInt<uint8_t>(2);
  data.writeBEInt<uint8_t>(3);
  data.writeBEInt<uint8_t>(4);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[00 01 02 03 04]");
}

TEST(PostgresMessage, EmptyArray) {
  auto f = std::bind(createMsg<Array<Int32>>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;

  data.writeBEInt<uint16_t>(0);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[Array of 0:{}]");
}

TEST(PostgresMessage, NoEmptyArrayOf32bitInts) {
  auto f = std::bind(createMsg<Array<Int32>>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;

  // Add 5 Int32 elements
  data.writeBEInt<uint16_t>(5);
  // Elements
  data.writeBEInt<uint32_t>(0);
  data.writeBEInt<uint32_t>(1);
  data.writeBEInt<uint32_t>(2);
  data.writeBEInt<uint32_t>(3);
  data.writeBEInt<uint32_t>(4);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[Array of 5:{[[00]][[01]][[02]][[03]][[04]]}]");
}

TEST(PostgresMessage, NoEmptyArrayOf16bitInts) {
  auto f = std::bind(createMsg<Array<Int16>>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;

  // Add 5 Int32 elements
  data.writeBEInt<uint16_t>(5);
  // Elements
  data.writeBEInt<uint16_t>(0);
  data.writeBEInt<uint16_t>(1);
  data.writeBEInt<uint16_t>(2);
  data.writeBEInt<uint16_t>(3);
  data.writeBEInt<uint16_t>(4);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[Array of 5:{[[00]][[01]][[02]][[03]][[04]]}]");
}

TEST(PostgresMessage, NoEmptyArrayOf8bitInts) {
  auto f = std::bind(createMsg<Array<Int8>>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;

  // Add 5 Int32 elements
  data.writeBEInt<uint16_t>(5);
  // Elements
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint8_t>(1);
  data.writeBEInt<uint8_t>(2);
  data.writeBEInt<uint8_t>(3);
  data.writeBEInt<uint8_t>(4);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[Array of 5:{[[00]][[01]][[02]][[03]][[04]]}]");
}

TEST(PostgresMessage, NoEmptyArrayOfStruct) {
  auto f = std::bind(createMsg<Array<Sequence<Int8>>>);
  // std::unique_ptr<MessageI> msg = std::make_unique<Array<Sequence<Int32>>>('B', 5);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;

  // Add 5 Int32 elements
  data.writeBEInt<uint16_t>(5);
  // Elements
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint8_t>(1);
  data.writeBEInt<uint8_t>(2);
  data.writeBEInt<uint8_t>(3);
  data.writeBEInt<uint8_t>(4);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[Array of 5:{[[00]][[01]][[02]][[03]][[04]]}]");
}

TEST(PostgresMessage, NoEmptyArrayOfStruct1) {
  auto f = std::bind(createMsg<Array<Sequence<Int8, Int16>>>);
  // std::unique_ptr<MessageI> msg = std::make_unique<Array<Sequence<Int32>>>('B', 5);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;

  // Add 5 Int32 elements
  data.writeBEInt<uint16_t>(5);
  // Elements
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint16_t>(100);
  data.writeBEInt<uint8_t>(1);
  data.writeBEInt<uint16_t>(101);
  data.writeBEInt<uint8_t>(2);
  data.writeBEInt<uint16_t>(102);
  data.writeBEInt<uint8_t>(3);
  data.writeBEInt<uint16_t>(103);
  data.writeBEInt<uint8_t>(4);
  data.writeBEInt<uint16_t>(104);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[Array of 5:{[[00][100]][[01][101]][[02][102]][[03][103]][[04][104]]}]");
}

TEST(PostgresMessage, EmptySingleByteN) {
  auto f = std::bind(createMsg<ByteN>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[]");
}
TEST(PostgresMessage, Byte1AndString) {
  auto f = std::bind(createMsg<Byte1, String>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;
  data.writeBEInt<uint8_t>('S');
  data.add("test");
  data.writeBEInt<uint32_t>(0);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[S][test]");
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
