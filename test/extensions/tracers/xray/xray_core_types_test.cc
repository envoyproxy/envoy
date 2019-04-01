#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "extensions/tracers/xray/util.h"
#include "extensions/tracers/xray/xray_core_constants.h"
#include "extensions/tracers/xray/xray_core_types.h"

#include "test/test_common/test_time.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {
TEST(XRayCoreTypesAnnotationTest, defaultConstructor) {
  Annotation ann;
  EXPECT_EQ("", ann.key());
  EXPECT_EQ("", ann.value());

  ann.setKey("key");
  EXPECT_EQ("key", ann.key());

  ann.setValue("value");
  EXPECT_EQ("value", ann.value());

  std::string expected_json = R"({"key":"value"})";
  EXPECT_EQ(expected_json, ann.toJson());
}

TEST(XRayCoreTypesAnnotationTest, customConstructor) {
  Annotation ann("key", "value");

  EXPECT_EQ("key", ann.key());
  EXPECT_EQ("value", ann.value());
  std::string expected_json = R"({"key":"value"})";
  EXPECT_EQ(expected_json, ann.toJson());
}

TEST(XRayCoreTypesAnnotationTest, copyConstructor) {
  Annotation ann("key", "value");
  Annotation ann2(ann);

  EXPECT_EQ(ann.value(), ann2.value());
  EXPECT_EQ(ann.key(), ann2.key());
  EXPECT_EQ(ann.toJson(), ann2.toJson());
}

TEST(XRayCoreTypesAnnotationTest, assignmentOperator) {
  Annotation ann("key", "value");
  Annotation ann2 = ann;

  EXPECT_EQ(ann.value(), ann2.value());
  EXPECT_EQ(ann.key(), ann2.key());
  EXPECT_EQ(ann.toJson(), ann2.toJson());
}

TEST(XRayCoreTypesSpanTest, defaultConstructor) {
  DangerousDeprecatedTestTime test_time;
  Span span(test_time.timeSystem());

  EXPECT_EQ(0ULL, span.id());
  EXPECT_EQ("", span.traceId());
  EXPECT_EQ("", span.name());
  EXPECT_EQ(0ULL, span.annotations().size());
  EXPECT_EQ("0000000000000000", span.idAsHexString());
  EXPECT_EQ("0000000000000000", span.parentIdAsHexString());
  EXPECT_EQ(0LL, span.startTime());
  EXPECT_FALSE(span.isSetParentId());

  uint64_t id = Util::generateRandom64(test_time.timeSystem());
  std::string id_hex = Hex::uint64ToHex(id);
  span.setId(id);
  EXPECT_EQ(id, span.id());
  EXPECT_EQ(id_hex, span.idAsHexString());

  id = Util::generateRandom64(test_time.timeSystem());
  id_hex = Hex::uint64ToHex(id);
  span.setParentId(id);
  EXPECT_EQ(id, span.parentId());
  EXPECT_EQ(id_hex, span.parentIdAsHexString());
  EXPECT_TRUE(span.isSetParentId());

  std::string trace_id = "1-abcdefg-hijklmn";
  span.setTraceId(trace_id);
  EXPECT_EQ(trace_id, span.traceId());

  double start_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                          test_time.timeSystem().monotonicTime().time_since_epoch())
                          .count() /
                      static_cast<double>(1000);
  span.setStartTime(start_time);
  EXPECT_EQ(start_time, span.startTime());

  span.setName("segment_name");
  EXPECT_EQ("segment_name", span.name());

  Annotation bann;
  std::vector<XRay::Annotation> annotations_;

  bann.setKey(XRay::XRayCoreConstants::get().UPSTREAM_CLUSTER);
  bann.setValue("test_upstream");

  annotations_.push_back(bann);
  span.setAnnotations(annotations_);
  EXPECT_EQ(1ULL, span.annotations().size());

  // Test the copy-semantics flavor of addAnnotation
  bann.setKey(XRay::XRayCoreConstants::get().HTTP_STATUS_CODE);
  bann.setValue("200");
  span.addAnnotation(bann);
  EXPECT_EQ(2ULL, span.annotations().size());

  // Test the move-semantics flavor of addAnnotation and addAnnotation
  bann.setKey(XRay::XRayCoreConstants::get().HTTP_STATUS_CODE);
  bann.setValue("400");
  span.addAnnotation(std::move(bann));
  EXPECT_EQ(3ULL, span.annotations().size());
}

TEST(XRayCoreTypesSpanTest, copyConstructor) {
  DangerousDeprecatedTestTime test_time;
  Span span(test_time.timeSystem());

  uint64_t id = Util::generateRandom64(test_time.timeSystem());
  std::string id_hex = Hex::uint64ToHex(id);
  span.setId(id);
  span.setParentId(id);
  std::string trace_id = "1-abcdefg-hijklmn";
  span.setTraceId(trace_id);
  double start_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                          test_time.timeSystem().monotonicTime().time_since_epoch())
                          .count() /
                      static_cast<double>(1000);
  span.setStartTime(start_time);
  span.setName("segment_name");

  Span span2(span);

  EXPECT_EQ(span.id(), span2.id());
  EXPECT_EQ(span.parentId(), span2.parentId());
  EXPECT_EQ(span.traceId(), span2.traceId());
  EXPECT_EQ(span.name(), span2.name());
  EXPECT_EQ(span.annotations().size(), span2.annotations().size());
  EXPECT_EQ(span.idAsHexString(), span2.idAsHexString());
  EXPECT_EQ(span.parentIdAsHexString(), span2.parentIdAsHexString());
  EXPECT_EQ(span.startTime(), span2.startTime());
  EXPECT_EQ(span.isSetParentId(), span2.isSetParentId());
}

TEST(XRayCoreTypesSpanTest, assignmentOperator) {
  DangerousDeprecatedTestTime test_time;
  Span span(test_time.timeSystem());

  uint64_t id = Util::generateRandom64(test_time.timeSystem());
  std::string id_hex = Hex::uint64ToHex(id);
  span.setId(id);
  span.setParentId(id);
  std::string trace_id = "1-abcdefg-hijklmn";
  span.setTraceId(trace_id);
  double start_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                          test_time.timeSystem().monotonicTime().time_since_epoch())
                          .count() /
                      static_cast<double>(1000);
  span.setStartTime(start_time);
  span.setName("segment_name");

  Span span2 = span;

  EXPECT_EQ(span.id(), span2.id());
  EXPECT_EQ(span.parentId(), span2.parentId());
  EXPECT_EQ(span.traceId(), span2.traceId());
  EXPECT_EQ(span.name(), span2.name());
  EXPECT_EQ(span.annotations().size(), span2.annotations().size());
  EXPECT_EQ(span.idAsHexString(), span2.idAsHexString());
  EXPECT_EQ(span.parentIdAsHexString(), span2.parentIdAsHexString());
  EXPECT_EQ(span.startTime(), span2.startTime());
  EXPECT_EQ(span.isSetParentId(), span2.isSetParentId());
}

TEST(XRayCoreTypesSpanTest, setTag) {
  DangerousDeprecatedTestTime test_time;
  Span span(test_time.timeSystem());

  span.setTag("key1", "value1");
  span.setTag("key2", "value2");

  EXPECT_EQ(2ULL, span.annotations().size());

  Annotation bann = span.annotations()[0];
  EXPECT_EQ("key1", bann.key());
  EXPECT_EQ("value1", bann.value());

  bann = span.annotations()[1];
  EXPECT_EQ("key2", bann.key());
  EXPECT_EQ("value2", bann.value());
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
