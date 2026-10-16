#include <utility>

#include "source/common/protobuf/arena_wrapped_proto.h"
#include "source/common/protobuf/protobuf.h"

#include "test/common/protobuf/arena_wrapped_proto_test.pb.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

TEST(ArenaWrappedProtoTest, AllocatesOnArena) {
  ArenaWrappedProto<test::TestMessage> wrapped;
  EXPECT_NE(wrapped.arena(), nullptr);
  EXPECT_NE(wrapped.get(), nullptr);
  EXPECT_EQ(wrapped.get()->GetArena(), wrapped.arena());
}

TEST(ArenaWrappedProtoTest, SubMessageAllocatedOnSameArena) {
  ArenaWrappedProto<test::TestMessage> wrapped;
  test::SubMessage* sub = wrapped->mutable_sub();
  EXPECT_NE(sub, nullptr);
  EXPECT_EQ(sub->GetArena(), wrapped.arena());
}

TEST(ArenaWrappedProtoTest, OperatorArrowConstAndNonConst) {
  ArenaWrappedProto<test::TestMessage> wrapped;
  wrapped->set_name("test_name");
  EXPECT_EQ(wrapped->name(), "test_name");

  const ArenaWrappedProto<test::TestMessage>& const_wrapped = wrapped;
  EXPECT_EQ(const_wrapped->name(), "test_name");
}

TEST(ArenaWrappedProtoTest, OperatorStarConstAndNonConst) {
  ArenaWrappedProto<test::TestMessage> wrapped;
  (*wrapped).set_name("test_name");
  EXPECT_EQ((*wrapped).name(), "test_name");

  const ArenaWrappedProto<test::TestMessage>& const_wrapped = wrapped;
  EXPECT_EQ((*const_wrapped).name(), "test_name");
}

TEST(ArenaWrappedProtoTest, ImplicitConversionConstAndNonConst) {
  ArenaWrappedProto<test::TestMessage> wrapped;
  wrapped->set_name("test_name");

  auto accepts_ref = [](test::TestMessage& ref) { EXPECT_EQ(ref.name(), "test_name"); };
  auto accepts_const_ref = [](const test::TestMessage& const_ref) {
    EXPECT_EQ(const_ref.name(), "test_name");
  };

  accepts_ref(wrapped);
  accepts_const_ref(wrapped);

  const ArenaWrappedProto<test::TestMessage>& const_wrapped = wrapped;
  accepts_const_ref(const_wrapped);
}

TEST(ArenaWrappedProtoTest, GetConstAndNonConst) {
  ArenaWrappedProto<test::TestMessage> wrapped;
  wrapped->set_name("test_name");
  EXPECT_EQ(wrapped.get()->name(), "test_name");

  const ArenaWrappedProto<test::TestMessage>& const_wrapped = wrapped;
  EXPECT_EQ(const_wrapped.get()->name(), "test_name");
}

TEST(ArenaWrappedProtoTest, MoveConstructor) {
  ArenaWrappedProto<test::TestMessage> wrapped;
  wrapped->set_name("moved_name");
  Protobuf::Arena* original_arena = wrapped.arena();
  test::TestMessage* original_proto = wrapped.get();

  ArenaWrappedProto<test::TestMessage> moved(std::move(wrapped));
  EXPECT_EQ(moved.arena(), original_arena);
  EXPECT_EQ(moved.get(), original_proto);
  EXPECT_EQ(moved->name(), "moved_name");
  EXPECT_EQ(moved.get()->GetArena(), moved.arena());
  // NOLINTBEGIN(bugprone-use-after-move)
  EXPECT_EQ(wrapped.arena(), nullptr);
  EXPECT_EQ(wrapped.get(), nullptr);
  // NOLINTEND(bugprone-use-after-move)
}

TEST(ArenaWrappedProtoTest, MoveAssignment) {
  ArenaWrappedProto<test::TestMessage> wrapped;
  wrapped->set_name("moved_name");
  Protobuf::Arena* original_arena = wrapped.arena();
  test::TestMessage* original_proto = wrapped.get();

  ArenaWrappedProto<test::TestMessage> moved;
  moved = std::move(wrapped);
  EXPECT_EQ(moved.arena(), original_arena);
  EXPECT_EQ(moved.get(), original_proto);
  EXPECT_EQ(moved->name(), "moved_name");
  EXPECT_EQ(moved.get()->GetArena(), moved.arena());
  // NOLINTBEGIN(bugprone-use-after-move)
  EXPECT_EQ(wrapped.arena(), nullptr);
  EXPECT_EQ(wrapped.get(), nullptr);
  // NOLINTEND(bugprone-use-after-move)
}

TEST(ArenaWrappedProtoTest, UniquePtrAdoptionConcrete) {
  auto heap_proto = std::make_unique<test::TestMessage>();
  heap_proto->set_name("concrete_heap");
  ArenaWrappedProto<test::TestMessage> wrapped(std::move(heap_proto));
  EXPECT_EQ(heap_proto, nullptr);
  EXPECT_NE(wrapped.arena(), nullptr);
  EXPECT_NE(wrapped.get(), nullptr);
  EXPECT_EQ(wrapped->name(), "concrete_heap");
  EXPECT_EQ(wrapped.get()->GetArena(), wrapped.arena());
}

TEST(ArenaWrappedProtoTest, UniquePtrAdoptionAbstract) {
  std::unique_ptr<Protobuf::Message> heap_proto = std::make_unique<test::TestMessage>();
  static_cast<test::TestMessage*>(heap_proto.get())->set_name("abstract_heap");
  ArenaWrappedProto<Protobuf::Message> wrapped(std::move(heap_proto));
  EXPECT_EQ(heap_proto, nullptr);
  EXPECT_NE(wrapped.arena(), nullptr);
  EXPECT_NE(wrapped.get(), nullptr);
  EXPECT_EQ(static_cast<const test::TestMessage&>(*wrapped).name(), "abstract_heap");
  EXPECT_EQ(wrapped.get()->GetArena(), wrapped.arena());
}

TEST(ArenaWrappedProtoTest, UniquePtrAdoptionNull) {
  std::unique_ptr<test::TestMessage> heap_proto = nullptr;
  ArenaWrappedProto<test::TestMessage> wrapped(std::move(heap_proto));
  EXPECT_EQ(wrapped.arena(), nullptr);
  EXPECT_EQ(wrapped.get(), nullptr);
  EXPECT_FALSE(wrapped);
}

TEST(ArenaWrappedProtoTest, ConstructWithOptions) {
  Protobuf::ArenaOptions options;
  options.start_block_size = 4096;
  options.max_block_size = 65536;
  ArenaWrappedProto<test::TestMessage> wrapped(options);
  EXPECT_NE(wrapped.arena(), nullptr);
  EXPECT_NE(wrapped.get(), nullptr);
  EXPECT_EQ(wrapped.get()->GetArena(), wrapped.arena());
  wrapped->set_name("options_test");
  EXPECT_EQ(wrapped->name(), "options_test");
}

TEST(ArenaWrappedProtoTest, MoveConversionWithOptions) {
  Protobuf::ArenaOptions options;
  options.start_block_size = 4096;
  options.max_block_size = 65536;
  ArenaWrappedProto<test::TestMessage> wrapped(options);
  wrapped->set_name("options_test");
  Protobuf::Arena* arena_ptr = wrapped.arena();

  ArenaWrappedProto<Protobuf::Message> base_wrapped(std::move(wrapped));
  EXPECT_EQ(base_wrapped.arena(), arena_ptr);
  EXPECT_NE(base_wrapped.get(), nullptr);
  EXPECT_EQ(static_cast<const test::TestMessage&>(*base_wrapped).name(), "options_test");
}

TEST(ArenaWrappedProtoTest, UniquePtrAdoptionWithOptions) {
  Protobuf::ArenaOptions options;
  options.start_block_size = 4096;
  auto heap_proto = std::make_unique<test::TestMessage>();
  heap_proto->set_name("heap_with_options");
  ArenaWrappedProto<test::TestMessage> wrapped(options, std::move(heap_proto));
  EXPECT_EQ(heap_proto, nullptr);
  EXPECT_NE(wrapped.arena(), nullptr);
  EXPECT_NE(wrapped.get(), nullptr);
  EXPECT_EQ(wrapped->name(), "heap_with_options");
  EXPECT_EQ(wrapped.get()->GetArena(), wrapped.arena());
}

} // namespace
} // namespace Envoy
