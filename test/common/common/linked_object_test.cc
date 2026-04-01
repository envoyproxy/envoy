#include "source/common/common/linked_object.h"

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
  auto removed = obj2_ptr->removeFromList(list);
  ASSERT_EQ(2u, list.size());
  ASSERT_FALSE(removed->inserted());
  ASSERT_EQ(obj1_ptr, list.front());
  ASSERT_EQ(obj3_ptr, list.back());

  // Remove front element.
  auto removed_front = obj1_ptr->removeFromList(list);
  ASSERT_EQ(1u, list.size());
  ASSERT_EQ(obj3_ptr, list.front());
  ASSERT_EQ(obj3_ptr, list.back());

  // Remove last element.
  auto removed_back = obj3_ptr->removeFromList(list);
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
  auto r1 = obj1_ptr->removeFromList(list);
  ASSERT_EQ(2u, list.size());
  ASSERT_FALSE(r1->inserted());
  ASSERT_EQ(obj2_ptr, list.front());
  ASSERT_EQ(obj3_ptr, list.back());

  // Remove back next.
  auto r3 = obj3_ptr->removeFromList(list);
  ASSERT_EQ(1u, list.size());
  ASSERT_FALSE(r3->inserted());
  ASSERT_EQ(obj2_ptr, list.front());
  ASSERT_EQ(obj2_ptr, list.back());

  // Remove middle (now the only element).
  auto r2 = obj2_ptr->removeFromList(list);
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
  auto r3 = obj3_ptr->removeFromList(list);
  ASSERT_EQ(2u, list.size());
  ASSERT_FALSE(r3->inserted());
  ASSERT_EQ(obj1_ptr, list.front());
  ASSERT_EQ(obj2_ptr, list.back());

  // Remove front next.
  auto r1 = obj1_ptr->removeFromList(list);
  ASSERT_EQ(1u, list.size());
  ASSERT_FALSE(r1->inserted());
  ASSERT_EQ(obj2_ptr, list.front());
  ASSERT_EQ(obj2_ptr, list.back());

  // Remove middle (now the only element).
  auto r2 = obj2_ptr->removeFromList(list);
  ASSERT_EQ(0u, list.size());
  ASSERT_TRUE(list.empty());
  ASSERT_FALSE(r2->inserted());
}

TEST(IntrusiveListNodeTest, MoveBetweenLists) {
  IntrusiveList<IntrusiveTestObject> src;
  IntrusiveList<IntrusiveTestObject> dst;

  auto obj1 = std::make_unique<IntrusiveTestObject>(1);
  auto obj2 = std::make_unique<IntrusiveTestObject>(2);
  auto obj3 = std::make_unique<IntrusiveTestObject>(3);
  IntrusiveTestObject* obj1_ptr = obj1.get();
  IntrusiveTestObject* obj2_ptr = obj2.get();
  IntrusiveTestObject* obj3_ptr = obj3.get();

  src.pushBack(std::move(obj1));
  src.pushBack(std::move(obj2));
  src.pushBack(std::move(obj3));

  // Move middle element to dst.
  obj2_ptr->moveBetweenLists(src, dst);
  ASSERT_EQ(2u, src.size());
  ASSERT_EQ(1u, dst.size());
  ASSERT_EQ(obj1_ptr, src.front());
  ASSERT_EQ(obj3_ptr, src.back());
  ASSERT_EQ(obj2_ptr, dst.front());
  ASSERT_TRUE(obj2_ptr->inserted());

  // Move front of src to dst (becomes new front).
  obj1_ptr->moveBetweenLists(src, dst);
  ASSERT_EQ(1u, src.size());
  ASSERT_EQ(2u, dst.size());
  ASSERT_EQ(obj3_ptr, src.front());
  ASSERT_EQ(obj1_ptr, dst.front());
  ASSERT_EQ(obj2_ptr, dst.back());
}

} // namespace Envoy
