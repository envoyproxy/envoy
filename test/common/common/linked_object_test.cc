#include <functional>
#include <type_traits>
#include <vector>

#include "source/common/common/linked_object.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

class TestObject : public LinkedObject<TestObject> {
public:
  TestObject() = default;
};

class IntrusiveTestObject : public IntrusiveListNode<IntrusiveTestObject> {
public:
  explicit IntrusiveTestObject(int value = 0) : value_(value) {}
  int value() const { return value_; }

private:
  int value_;
};

class DestructorProbeObject : public IntrusiveListNode<DestructorProbeObject> {
public:
  using OnDestroy = std::function<void(DestructorProbeObject&)>;
  DestructorProbeObject(int value, OnDestroy on_destroy)
      : value_(value), on_destroy_(std::move(on_destroy)) {}
  ~DestructorProbeObject() { on_destroy_(*this); }
  int value() const { return value_; }

private:
  int value_;
  OnDestroy on_destroy_;
};

TEST(LinkedObjectTest, MoveIntoListFront) {
  std::list<std::unique_ptr<TestObject>> list;
  auto object = std::make_unique<TestObject>();
  TestObject* object_ptr = object.get();
  LinkedList::moveIntoList(std::move(object), list);
  ASSERT_EQ(1, list.size());
  ASSERT_EQ(object_ptr, list.front().get());

  auto object2 = std::make_unique<TestObject>();
  TestObject* object2_ptr = object2.get();
  LinkedList::moveIntoList(std::move(object2), list);
  ASSERT_EQ(2, list.size());
  ASSERT_EQ(object2_ptr, list.front().get());
  ASSERT_EQ(object_ptr, list.back().get());
}

TEST(LinkedObjectTest, MoveIntoListBack) {
  std::list<std::unique_ptr<TestObject>> list;
  std::unique_ptr<TestObject> object = std::make_unique<TestObject>();
  TestObject* object_ptr = object.get();
  LinkedList::moveIntoListBack(std::move(object), list);
  ASSERT_EQ(1, list.size());
  ASSERT_EQ(object_ptr, list.front().get());

  auto object2 = std::make_unique<TestObject>();
  TestObject* object2_ptr = object2.get();
  LinkedList::moveIntoListBack(std::move(object2), list);
  ASSERT_EQ(2, list.size());
  ASSERT_EQ(object2_ptr, list.back().get());
  ASSERT_EQ(object_ptr, list.front().get());
}

TEST(IntrusiveListNodeTest, MoveIntoListFront) {
  IntrusiveList<IntrusiveTestObject> list;
  ASSERT_TRUE(list.empty());

  auto obj1 = std::make_unique<IntrusiveTestObject>(1);
  IntrusiveTestObject* obj1_ptr = obj1.get();
  list.push(std::move(obj1));
  ASSERT_EQ(1u, list.size());
  ASSERT_FALSE(list.empty());
  ASSERT_EQ(obj1_ptr, list.front());
  ASSERT_EQ(obj1_ptr, list.back());
  ASSERT_TRUE(obj1_ptr->inserted());

  auto obj2 = std::make_unique<IntrusiveTestObject>(2);
  IntrusiveTestObject* obj2_ptr = obj2.get();
  list.push(std::move(obj2));
  ASSERT_EQ(2u, list.size());
  ASSERT_EQ(obj2_ptr, list.front());
  ASSERT_EQ(obj1_ptr, list.back());
}

TEST(IntrusiveListNodeTest, MoveIntoListBack) {
  IntrusiveList<IntrusiveTestObject> list;

  auto obj1 = std::make_unique<IntrusiveTestObject>(1);
  IntrusiveTestObject* obj1_ptr = obj1.get();
  list.pushBack(std::move(obj1));
  ASSERT_EQ(1u, list.size());
  ASSERT_EQ(obj1_ptr, list.front());
  ASSERT_EQ(obj1_ptr, list.back());

  auto obj2 = std::make_unique<IntrusiveTestObject>(2);
  IntrusiveTestObject* obj2_ptr = obj2.get();
  list.pushBack(std::move(obj2));
  ASSERT_EQ(2u, list.size());
  ASSERT_EQ(obj1_ptr, list.front());
  ASSERT_EQ(obj2_ptr, list.back());
}

TEST(IntrusiveListNodeTest, RemoveFromList) {
  IntrusiveList<IntrusiveTestObject> list;

  auto obj1 = std::make_unique<IntrusiveTestObject>(1);
  auto obj2 = std::make_unique<IntrusiveTestObject>(2);
  auto obj3 = std::make_unique<IntrusiveTestObject>(3);
  IntrusiveTestObject* obj1_ptr = obj1.get();
  IntrusiveTestObject* obj2_ptr = obj2.get();
  IntrusiveTestObject* obj3_ptr = obj3.get();

  list.pushBack(std::move(obj1));
  list.pushBack(std::move(obj2));
  list.pushBack(std::move(obj3));
  ASSERT_EQ(3u, list.size());

  // Remove middle element.
  auto removed = obj2_ptr->removeFromList();
  ASSERT_EQ(2u, list.size());
  ASSERT_FALSE(removed->inserted());
  ASSERT_EQ(obj1_ptr, list.front());
  ASSERT_EQ(obj3_ptr, list.back());

  // Remove front element.
  auto removed_front = obj1_ptr->removeFromList();
  ASSERT_EQ(1u, list.size());
  ASSERT_EQ(obj3_ptr, list.front());
  ASSERT_EQ(obj3_ptr, list.back());

  // Remove last element.
  auto removed_back = obj3_ptr->removeFromList();
  ASSERT_EQ(0u, list.size());
  ASSERT_TRUE(list.empty());
}

TEST(IntrusiveListNodeTest, RemoveFromListFrontThenBackThenMiddle) {
  IntrusiveList<IntrusiveTestObject> list;

  auto obj1 = std::make_unique<IntrusiveTestObject>(1);
  auto obj2 = std::make_unique<IntrusiveTestObject>(2);
  auto obj3 = std::make_unique<IntrusiveTestObject>(3);
  IntrusiveTestObject* obj1_ptr = obj1.get();
  IntrusiveTestObject* obj2_ptr = obj2.get();
  IntrusiveTestObject* obj3_ptr = obj3.get();

  list.pushBack(std::move(obj1));
  list.pushBack(std::move(obj2));
  list.pushBack(std::move(obj3));

  // Remove front first.
  auto r1 = obj1_ptr->removeFromList();
  ASSERT_EQ(2u, list.size());
  ASSERT_FALSE(r1->inserted());
  ASSERT_EQ(obj2_ptr, list.front());
  ASSERT_EQ(obj3_ptr, list.back());

  // Remove back next.
  auto r3 = obj3_ptr->removeFromList();
  ASSERT_EQ(1u, list.size());
  ASSERT_FALSE(r3->inserted());
  ASSERT_EQ(obj2_ptr, list.front());
  ASSERT_EQ(obj2_ptr, list.back());

  // Remove middle (now the only element).
  auto r2 = obj2_ptr->removeFromList();
  ASSERT_EQ(0u, list.size());
  ASSERT_TRUE(list.empty());
  ASSERT_FALSE(r2->inserted());
}

TEST(IntrusiveListNodeTest, RemoveFromListBackThenFrontThenMiddle) {
  IntrusiveList<IntrusiveTestObject> list;

  auto obj1 = std::make_unique<IntrusiveTestObject>(1);
  auto obj2 = std::make_unique<IntrusiveTestObject>(2);
  auto obj3 = std::make_unique<IntrusiveTestObject>(3);
  IntrusiveTestObject* obj1_ptr = obj1.get();
  IntrusiveTestObject* obj2_ptr = obj2.get();
  IntrusiveTestObject* obj3_ptr = obj3.get();

  list.pushBack(std::move(obj1));
  list.pushBack(std::move(obj2));
  list.pushBack(std::move(obj3));

  // Remove back first.
  auto r3 = obj3_ptr->removeFromList();
  ASSERT_EQ(2u, list.size());
  ASSERT_FALSE(r3->inserted());
  ASSERT_EQ(obj1_ptr, list.front());
  ASSERT_EQ(obj2_ptr, list.back());

  // Remove front next.
  auto r1 = obj1_ptr->removeFromList();
  ASSERT_EQ(1u, list.size());
  ASSERT_FALSE(r1->inserted());
  ASSERT_EQ(obj2_ptr, list.front());
  ASSERT_EQ(obj2_ptr, list.back());

  // Remove middle (now the only element).
  auto r2 = obj2_ptr->removeFromList();
  ASSERT_EQ(0u, list.size());
  ASSERT_TRUE(list.empty());
  ASSERT_FALSE(r2->inserted());
}

TEST(IntrusiveListNodeTest, ListDestructorDetachesNodeBeforeDeletingIt) {
  struct Observation {
    int value;
    bool inserted;
    bool prev_null;
    bool next_null;
    size_t list_size;
    int front_value; // -1 when the list is empty.
    bool front_prev_null;
    int back_value; // -1 when the list is empty.
  };
  std::vector<Observation> observations;
  {
    IntrusiveList<DestructorProbeObject> list;
    auto on_destroy = [&list, &observations](DestructorProbeObject& self) {
      const DestructorProbeObject* front = list.front();
      const DestructorProbeObject* back = list.back();
      observations.push_back(
          {self.value(), self.inserted(), self.prev() == nullptr, self.next() == nullptr,
           list.size(), front != nullptr ? front->value() : -1,
           front == nullptr || front->prev() == nullptr, back != nullptr ? back->value() : -1});
      // A self-unlinking destructor must see the node as already detached.
      if (self.inserted()) {
        static_cast<void>(self.removeFromList().release());
      }
    };
    list.pushBack(std::make_unique<DestructorProbeObject>(1, on_destroy));
    list.pushBack(std::make_unique<DestructorProbeObject>(2, on_destroy));
    list.pushBack(std::make_unique<DestructorProbeObject>(3, on_destroy));
  }

  ASSERT_EQ(3u, observations.size());
  for (size_t i = 0; i < observations.size(); i++) {
    const Observation& o = observations[i];
    EXPECT_EQ(static_cast<int>(i) + 1, o.value);
    EXPECT_FALSE(o.inserted);
    EXPECT_TRUE(o.prev_null);
    EXPECT_TRUE(o.next_null);
    EXPECT_EQ(2u - i, o.list_size);
    EXPECT_TRUE(o.front_prev_null);
  }
  EXPECT_EQ(2, observations[0].front_value);
  EXPECT_EQ(3, observations[0].back_value);
  EXPECT_EQ(3, observations[1].front_value);
  EXPECT_EQ(3, observations[1].back_value);
  EXPECT_EQ(-1, observations[2].front_value);
  EXPECT_EQ(-1, observations[2].back_value);
}

TEST(IntrusiveListNodeTest, ListDestructorDeletesFrontToBack) {
  std::vector<int> destroyed;
  bool empty_after_last = false;
  {
    IntrusiveList<DestructorProbeObject> list;
    auto on_destroy = [&list, &destroyed, &empty_after_last](DestructorProbeObject& self) {
      destroyed.push_back(self.value());
      empty_after_last = list.empty() && list.front() == nullptr && list.back() == nullptr;
    };
    list.pushBack(std::make_unique<DestructorProbeObject>(2, on_destroy));
    list.push(std::make_unique<DestructorProbeObject>(1, on_destroy));
    list.pushBack(std::make_unique<DestructorProbeObject>(3, on_destroy));
  }
  EXPECT_EQ((std::vector<int>{1, 2, 3}), destroyed);
  // Observed while deleting the last node, which is already detached.
  EXPECT_TRUE(empty_after_last);
}

TEST(IntrusiveListNodeTest, RemoveWhenNotInsertedIsEnvoyBug) {
  auto obj = std::make_unique<IntrusiveTestObject>(1);
  EXPECT_ENVOY_BUG(EXPECT_EQ(nullptr, obj->removeFromList()), "removed while not in a list");

  // Also after it was removed from a list.
  IntrusiveList<IntrusiveTestObject> list;
  list.pushBack(std::move(obj));
  obj = list.front()->removeFromList();
  ASSERT_NE(nullptr, obj);
  EXPECT_ENVOY_BUG(EXPECT_EQ(nullptr, obj->removeFromList()), "removed while not in a list");
  EXPECT_TRUE(list.empty());
}

#if !defined(NDEBUG)
TEST(IntrusiveListNodeDeathTest, DoublePushAborts) {
  IntrusiveList<IntrusiveTestObject> list;
  IntrusiveList<IntrusiveTestObject> other;
  list.push(std::make_unique<IntrusiveTestObject>(1));
  IntrusiveTestObject* obj_ptr = list.front();

  // The aliasing unique_ptr is only created in the death test child.
  EXPECT_DEATH(list.push(std::unique_ptr<IntrusiveTestObject>(obj_ptr)),
               "pushed while already in a list");
  EXPECT_DEATH(list.pushBack(std::unique_ptr<IntrusiveTestObject>(obj_ptr)),
               "pushed while already in a list");
  EXPECT_DEATH(other.push(std::unique_ptr<IntrusiveTestObject>(obj_ptr)),
               "pushed while already in a list");
  EXPECT_DEATH(other.pushBack(std::unique_ptr<IntrusiveTestObject>(obj_ptr)),
               "pushed while already in a list");

  EXPECT_EQ(1u, list.size());
  EXPECT_TRUE(other.empty());
}
#endif

TEST(IntrusiveListNodeTest, PrevChainAfterPush) {
  IntrusiveList<IntrusiveTestObject> list;
  list.push(std::make_unique<IntrusiveTestObject>(3));
  IntrusiveTestObject* obj3_ptr = list.front();
  EXPECT_EQ(nullptr, obj3_ptr->prev());
  EXPECT_EQ(nullptr, obj3_ptr->next());

  list.push(std::make_unique<IntrusiveTestObject>(2));
  list.push(std::make_unique<IntrusiveTestObject>(1));
  IntrusiveTestObject* obj1_ptr = list.front();
  IntrusiveTestObject* obj2_ptr = obj1_ptr->next();

  // List is 1 <-> 2 <-> 3.
  EXPECT_EQ(2, obj2_ptr->value());
  EXPECT_EQ(obj3_ptr, list.back());
  EXPECT_EQ(nullptr, obj1_ptr->prev());
  EXPECT_EQ(obj1_ptr, obj2_ptr->prev());
  EXPECT_EQ(obj2_ptr, obj3_ptr->prev());
  EXPECT_EQ(obj3_ptr, obj2_ptr->next());
  EXPECT_EQ(nullptr, obj3_ptr->next());
}

TEST(IntrusiveListNodeTest, PrevChainAfterPushBack) {
  IntrusiveList<IntrusiveTestObject> list;
  list.pushBack(std::make_unique<IntrusiveTestObject>(1));
  IntrusiveTestObject* obj1_ptr = list.back();
  EXPECT_EQ(nullptr, obj1_ptr->prev());
  EXPECT_EQ(nullptr, obj1_ptr->next());

  list.pushBack(std::make_unique<IntrusiveTestObject>(2));
  IntrusiveTestObject* obj2_ptr = list.back();
  list.pushBack(std::make_unique<IntrusiveTestObject>(3));
  IntrusiveTestObject* obj3_ptr = list.back();

  // List is 1 <-> 2 <-> 3.
  EXPECT_EQ(obj1_ptr, list.front());
  EXPECT_EQ(nullptr, obj1_ptr->prev());
  EXPECT_EQ(obj1_ptr, obj2_ptr->prev());
  EXPECT_EQ(obj2_ptr, obj3_ptr->prev());
  EXPECT_EQ(obj2_ptr, obj1_ptr->next());
  EXPECT_EQ(obj3_ptr, obj2_ptr->next());
  EXPECT_EQ(nullptr, obj3_ptr->next());
}

TEST(IntrusiveListNodeTest, PrevChainAfterRemoveHeadTailAndMiddle) {
  IntrusiveList<IntrusiveTestObject> list;
  std::vector<IntrusiveTestObject*> ptrs;
  for (int i = 1; i <= 5; i++) {
    list.pushBack(std::make_unique<IntrusiveTestObject>(i));
    ptrs.push_back(list.back());
  }

  // Remove head: 2 <-> 3 <-> 4 <-> 5.
  auto r1 = ptrs[0]->removeFromList();
  EXPECT_EQ(nullptr, r1->prev());
  EXPECT_EQ(nullptr, r1->next());
  EXPECT_EQ(ptrs[1], list.front());
  EXPECT_EQ(nullptr, ptrs[1]->prev());
  EXPECT_EQ(ptrs[1], ptrs[2]->prev());

  // Remove tail: 2 <-> 3 <-> 4.
  auto r5 = ptrs[4]->removeFromList();
  EXPECT_EQ(nullptr, r5->prev());
  EXPECT_EQ(nullptr, r5->next());
  EXPECT_EQ(ptrs[3], list.back());
  EXPECT_EQ(nullptr, ptrs[3]->next());
  EXPECT_EQ(ptrs[2], ptrs[3]->prev());

  // Remove middle: 2 <-> 4.
  auto r3 = ptrs[2]->removeFromList();
  EXPECT_EQ(nullptr, r3->prev());
  EXPECT_EQ(nullptr, r3->next());
  EXPECT_EQ(2u, list.size());
  EXPECT_EQ(ptrs[1], list.front());
  EXPECT_EQ(ptrs[3], list.back());
  EXPECT_EQ(nullptr, ptrs[1]->prev());
  EXPECT_EQ(ptrs[3], ptrs[1]->next());
  EXPECT_EQ(ptrs[1], ptrs[3]->prev());
  EXPECT_EQ(nullptr, ptrs[3]->next());
}

TEST(IntrusiveListNodeTest, NodeIsNotCopyableOrMovable) {
  static_assert(!std::is_copy_constructible_v<IntrusiveTestObject>);
  static_assert(!std::is_copy_assignable_v<IntrusiveTestObject>);
  static_assert(!std::is_move_constructible_v<IntrusiveTestObject>);
  static_assert(!std::is_move_assignable_v<IntrusiveTestObject>);
}

TEST(IntrusiveListNodeTest, PushNullIsIgnored) {
  IntrusiveList<IntrusiveTestObject> list;
  list.push(std::unique_ptr<IntrusiveTestObject>());
  list.pushBack(std::unique_ptr<IntrusiveTestObject>());
  EXPECT_TRUE(list.empty());
  EXPECT_EQ(nullptr, list.front());
  EXPECT_EQ(nullptr, list.back());

  list.pushBack(std::make_unique<IntrusiveTestObject>(1));
  list.push(std::unique_ptr<IntrusiveTestObject>());
  list.pushBack(std::unique_ptr<IntrusiveTestObject>());
  ASSERT_EQ(1u, list.size());
  EXPECT_EQ(list.front(), list.back());
  EXPECT_EQ(1, list.front()->value());
}

#if !defined(NDEBUG)
TEST(IntrusiveListNodeDeathTest, DestroyWhileInsertedAborts) {
  IntrusiveList<IntrusiveTestObject> list;
  list.pushBack(std::make_unique<IntrusiveTestObject>(1));
  EXPECT_DEATH(delete list.front(), "destroyed while in a list");
  EXPECT_EQ(1u, list.size());
}
#endif

} // namespace Envoy
