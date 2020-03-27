#include <memory>

#include "common/http/status_or.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

TEST(StatusOr, Status) {
  StatusOr<int> status_or = Status(StatusCode::CodecProtocolError, "foobar");
  EXPECT_FALSE(status_or.Ok());
  EXPECT_FALSE(status_or.Status().Ok());
  EXPECT_EQ(StatusCode::CodecProtocolError, status_or.Status().Code());
  EXPECT_EQ("foobar", status_or.Status().Message());
}

TEST(StatusOr, CopyStatus) {
  StatusOr<int> status_or = Status(StatusCode::CodecProtocolError, "foobar");
  StatusOr<int> status_or_copy(status_or); // NOLINT - unit test
  EXPECT_FALSE(status_or_copy.Ok());
  EXPECT_FALSE(status_or_copy.Status().Ok());
  EXPECT_EQ(StatusCode::CodecProtocolError, status_or_copy.Status().Code());
  EXPECT_EQ("foobar", status_or_copy.Status().Message());
}

TEST(StatusOr, CopyErrorIntoValue) {
  StatusOr<int> status_or = Status(StatusCode::CodecProtocolError, "foobar");
  StatusOr<int> status_or_copy(1234);
  status_or_copy = status_or;
  EXPECT_FALSE(status_or_copy.Ok());
  EXPECT_FALSE(status_or_copy.Status().Ok());
  EXPECT_EQ(StatusCode::CodecProtocolError, status_or_copy.Status().Code());
  EXPECT_EQ("foobar", status_or_copy.Status().Message());
}

TEST(StatusOr, MoveStatus) {
  StatusOr<int> status_or = Status(StatusCode::CodecProtocolError, "foobar");
  StatusOr<int> status_or_copy(std::move(status_or));

  // status_or should retain the error status
  EXPECT_FALSE(status_or.Ok()); // NOLINT - unit test
  EXPECT_FALSE(status_or.Status().Ok());
  EXPECT_EQ(StatusCode::CodecProtocolError, status_or.Status().Code());
  EXPECT_EQ("foobar", status_or.Status().Message());

  EXPECT_FALSE(status_or_copy.Ok());
  EXPECT_FALSE(status_or_copy.Status().Ok());
  EXPECT_EQ(StatusCode::CodecProtocolError, status_or_copy.Status().Code());
  EXPECT_EQ("foobar", status_or_copy.Status().Message());
}

TEST(StatusOr, Value) {
  StatusOr<int> status_or = 55;
  EXPECT_TRUE(status_or.Ok());
  EXPECT_TRUE(status_or.Status().Ok());
  EXPECT_EQ(StatusCode::Ok, status_or.Status().Code());
  EXPECT_EQ("", status_or.Status().Message());
  EXPECT_EQ(55, status_or.Value());
}

TEST(StatusOr, CopyValue) {
  StatusOr<int> status_or = 55;
  StatusOr<int> status_or_copy = status_or;
  EXPECT_TRUE(status_or_copy.Ok());
  EXPECT_TRUE(status_or_copy.Status().Ok());
  EXPECT_EQ(StatusCode::Ok, status_or_copy.Status().Code());
  EXPECT_EQ("", status_or_copy.Status().Message());
  EXPECT_EQ(55, status_or_copy.Value());
}

TEST(StatusOr, CopyValueIntoError) {
  StatusOr<int> status_or = 55;
  StatusOr<int> status_or_copy(Status(StatusCode::CodecProtocolError, "foobar"));
  status_or_copy = status_or;
  EXPECT_TRUE(status_or_copy.Ok());
  EXPECT_TRUE(status_or_copy.Status().Ok());
  EXPECT_EQ(StatusCode::Ok, status_or_copy.Status().Code());
  EXPECT_EQ("", status_or_copy.Status().Message());
  EXPECT_EQ(55, status_or_copy.Value());
}

// Validate that StatusOr works with move only types
TEST(StatusOr, MoveOnlyValue) {
  StatusOr<std::unique_ptr<int>> status_or(std::make_unique<int>(55));
  EXPECT_TRUE(status_or.Ok());
  EXPECT_TRUE(status_or.Status().Ok());
  EXPECT_EQ(StatusCode::Ok, status_or.Status().Code());
  EXPECT_EQ("", status_or.Status().Message());
  EXPECT_EQ(55, *status_or.Value());

  StatusOr<std::unique_ptr<int>> another_status_or(std::move(status_or));
  EXPECT_EQ(55, *another_status_or.Value());

  std::unique_ptr<int> value(std::move(another_status_or).Value());
  EXPECT_EQ(55, *value);
  EXPECT_EQ(nullptr, another_status_or.Value()); // NOLINT - unit test
  EXPECT_TRUE(another_status_or.Ok());
}

TEST(StatusOr, CopyConversion) {
  struct A {
    A(int m) : m_(m) {}
    int m_{};
  };
  struct B {
    B(const A& a) : n_(10 + a.m_) {}
    int n_{};
  };

  StatusOr<A> a = A(12);
  StatusOr<B> b(a);
  EXPECT_EQ(22, b.Value().n_);
}

TEST(StatusOr, MoveConversion) {
  struct A {
    A(int m) : m_(m) {}
    virtual ~A() = default;
    int m_{};
  };
  struct B : public A {
    B(int n) : A(10 + n), n_(n) {}
    int n_{};
  };

  StatusOr<std::unique_ptr<B>> b(std::make_unique<B>(121));
  StatusOr<std::unique_ptr<A>> a(std::move(b));
  EXPECT_EQ(131, a.Value()->m_);
  EXPECT_EQ(nullptr, b.Value()); // NOLINT - unit test
}

} // namespace Http
} // namespace Envoy
