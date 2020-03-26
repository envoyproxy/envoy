#include "common/http/status.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

TEST(Status, Defaults) {
  Status status;
  EXPECT_EQ(sizeof(void*), sizeof(status));
  EXPECT_TRUE(status.Ok());
  EXPECT_EQ(StatusCode::Ok, status.Code());
  EXPECT_FALSE(status.HttpCode().has_value());
  EXPECT_TRUE(status.Message().empty());
  EXPECT_EQ("OK", status.ToString());
}

TEST(Status, OkStatusIgnoresOtherConstructorParameters) {
  Status status(StatusCode::Ok, "foobar");
  EXPECT_TRUE(status.Ok());
  EXPECT_EQ(StatusCode::Ok, status.Code());
  EXPECT_FALSE(status.HttpCode().has_value());
  EXPECT_TRUE(status.Message().empty());
  EXPECT_EQ("OK", status.ToString());

  Status another_status(StatusCode::Ok, Http::Code::InternalServerError, "foobar");
  EXPECT_TRUE(another_status.Ok());
  EXPECT_EQ(StatusCode::Ok, another_status.Code());
  EXPECT_FALSE(another_status.HttpCode().has_value());
  EXPECT_TRUE(another_status.Message().empty());
  EXPECT_EQ("OK", another_status.ToString());
}

TEST(Status, ErrorAndMessage) {
  Status status(StatusCode::CodecProtocolError, "foobar");
  EXPECT_FALSE(status.Ok());
  EXPECT_EQ(StatusCode::CodecProtocolError, status.Code());
  EXPECT_FALSE(status.HttpCode().has_value());
  EXPECT_EQ("foobar", status.Message());
  EXPECT_EQ("CodecProtocolError: foobar", status.ToString());
}

TEST(Status, ErrorHttpCodeAndMessage) {
  Status status(StatusCode::BufferFloodError, Http::Code::BadRequest, "foobar");
  EXPECT_FALSE(status.Ok());
  EXPECT_EQ(StatusCode::BufferFloodError, status.Code());
  EXPECT_TRUE(status.HttpCode().has_value());
  EXPECT_EQ(Http::Code::BadRequest, status.HttpCode().value());
  EXPECT_EQ("foobar", status.Message());
  EXPECT_EQ("BufferFloodError, HTTP code 400: foobar", status.ToString());
}

TEST(Status, MoveOkIntoOk) {
  Status status;
  Status another_status(std::move(status));
  EXPECT_TRUE(status.Ok());
  EXPECT_TRUE(another_status.Ok());
}

TEST(Status, MoveOkIntoError) {
  Status status(StatusCode::BufferFloodError, Http::Code::BadRequest, "foobar");
  Status another_status;
  status = std::move(another_status);
  EXPECT_TRUE(status.Ok());
  EXPECT_TRUE(another_status.Ok());
}

TEST(Status, MoveErrorIntoOk) {
  Status status(StatusCode::BufferFloodError, Http::Code::BadRequest, "foobar");
  Status another_status(std::move(status));
  EXPECT_TRUE(status.Ok());
  EXPECT_FALSE(another_status.Ok());
  EXPECT_EQ(StatusCode::BufferFloodError, another_status.Code());
  EXPECT_TRUE(another_status.HttpCode().has_value());
  EXPECT_EQ(Http::Code::BadRequest, another_status.HttpCode().value());
  EXPECT_EQ("foobar", another_status.Message());
}

TEST(Status, CopyOkIntoOk) {
  Status status;
  Status another_status(status);
  EXPECT_TRUE(status.Ok());
  EXPECT_TRUE(another_status.Ok());
}

TEST(Status, CopyOkIntoError) {
  Status status(StatusCode::BufferFloodError, Http::Code::BadRequest, "foobar");
  Status another_status;
  status = another_status;
  EXPECT_TRUE(status.Ok());
  EXPECT_TRUE(another_status.Ok());
}

TEST(Status, CopyErrorIntoOk) {
  Status status(StatusCode::BufferFloodError, Http::Code::BadRequest, "foobar");
  Status another_status(status);
  EXPECT_FALSE(status.Ok());
  EXPECT_EQ(StatusCode::BufferFloodError, status.Code());
  EXPECT_TRUE(status.HttpCode().has_value());
  EXPECT_EQ(Http::Code::BadRequest, status.HttpCode().value());
  EXPECT_EQ("foobar", status.Message());

  EXPECT_FALSE(another_status.Ok());
  EXPECT_EQ(StatusCode::BufferFloodError, another_status.Code());
  EXPECT_TRUE(another_status.HttpCode().has_value());
  EXPECT_EQ(Http::Code::BadRequest, another_status.HttpCode().value());
  EXPECT_EQ("foobar", another_status.Message());
}

} // namespace Http
} // namespace Envoy
