#include "source/common/common/linked_object.h"

#include "gtest/gtest.h"

namespace Envoy {

class TestObject : public LinkedObject<TestObject> {
public:
  TestObject() = default;
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

} // namespace Envoy
