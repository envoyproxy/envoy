#include <atomic>
#include <iostream>
#include <string>
#include <thread>

#include "common/log_sink/log_obj_store.h"

#include "gtest/gtest.h"

namespace Envoy {

namespace LogTransport {

TEST(LogObjStore, InsertTest) {
  LogObjectStore<int> los(1);

  EXPECT_FALSE(los.isFull());

  auto& ref = los.getNextWritableEntry();
  ref = 1;

  EXPECT_EQ(los.size(), 1);
  EXPECT_TRUE(los.isFull());
}

TEST(LogObjStore, IterableTest) {
  LogObjectStore<int> los(100);

  for (int i = 0; i < 100; i++) {
    EXPECT_FALSE(los.isFull());
    auto& ref = los.getNextWritableEntry();
    ref = i;
  }
  EXPECT_TRUE(los.isFull());

  int expected = 0;
  for (const auto& ref : los) {
    EXPECT_EQ(ref, expected++);
  }
}

TEST(LogObjStore, HalfFullIterableTest) {
  LogObjectStore<int> los(100);

  // Since there's nothing loaded this loop should execute no iterations:
  for (const auto& ref : los) {
    EXPECT_TRUE(false);
    EXPECT_EQ(ref, 1234567);
  }

  for (int i = 0; i < 50; i++) {
    EXPECT_FALSE(los.isFull());
    auto& ref = los.getNextWritableEntry();
    ref = i;
  }
  EXPECT_FALSE(los.isFull());
  EXPECT_EQ(los.size(), 50);

  int expected = 0;
  for (const auto& ref : los) {
    EXPECT_EQ(ref, expected++);
  }
  // Range-based for should have made exactly 50 iterations:
  EXPECT_EQ(expected, 50);
}

TEST(LogObjStore, ClearTest) {
  LogObjectStore<std::vector<int>> los(10);

  for (int i = 0; !los.isFull(); i++) {
    auto& ref = los.getNextWritableEntry();
    ref.push_back(i);
  }
  EXPECT_TRUE(los.isFull());

  int expected = 0;
  los.clear([&expected](std::vector<int>& v) {
    EXPECT_EQ(v.size(), 1);
    EXPECT_EQ(v[0], expected++);
    v.clear();
  });

  EXPECT_EQ(los.size(), 0);

  auto& ref = los.getNextWritableEntry();
  // The entry should have been cleared by the lambda above
  EXPECT_EQ(ref.size(), 0);
}

} // LogTransport
} // Envoy
