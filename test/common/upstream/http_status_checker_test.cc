#include "common/protobuf/protobuf.h"
#include "common/upstream/http_status_checker.h"

#include "test/common/upstream/utility.h"
#include "test/test_common/test_base.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Upstream {

TEST(HttpStatusChecker, Default) {
  const std::string yaml = R"EOF(
  http_health_check:
    service_name: locations
    path: /healthcheck
  )EOF";

  auto http_status_checker = HttpStatusChecker::configure(
      parseHealthCheckFromV2Yaml(yaml).http_health_check().expected_statuses(), 200);

  EXPECT_TRUE(http_status_checker->isExpected(200));
  EXPECT_FALSE(http_status_checker->isExpected(204));
}

TEST(HttpStatusChecker, Single100) {
  const std::string yaml = R"EOF(
  http_health_check:
    service_name: locations
    path: /healthcheck
    expected_statuses:
      - start: 100
        end: 101
  )EOF";

  auto http_status_checker = HttpStatusChecker::configure(
      parseHealthCheckFromV2Yaml(yaml).http_health_check().expected_statuses(), 200);

  EXPECT_FALSE(http_status_checker->isExpected(200));

  EXPECT_FALSE(http_status_checker->isExpected(99));
  EXPECT_TRUE(http_status_checker->isExpected(100));
  EXPECT_FALSE(http_status_checker->isExpected(101));
}

TEST(HttpStatusChecker, Single599) {
  const std::string yaml = R"EOF(
  http_health_check:
    service_name: locations
    path: /healthcheck
    expected_statuses:
      - start: 599
        end: 600
  )EOF";

  auto http_status_checker = HttpStatusChecker::configure(
      parseHealthCheckFromV2Yaml(yaml).http_health_check().expected_statuses(), 200);

  EXPECT_FALSE(http_status_checker->isExpected(200));

  EXPECT_FALSE(http_status_checker->isExpected(598));
  EXPECT_TRUE(http_status_checker->isExpected(599));
  EXPECT_FALSE(http_status_checker->isExpected(600));
}

TEST(HttpStatusChecker, Ranges_204_304) {
  const std::string yaml = R"EOF(
  http_health_check:
    service_name: locations
    path: /healthcheck
    expected_statuses:
      - start: 204
        end: 205
      - start: 304
        end: 305
  )EOF";

  auto http_status_checker = HttpStatusChecker::configure(
      parseHealthCheckFromV2Yaml(yaml).http_health_check().expected_statuses(), 200);

  EXPECT_FALSE(http_status_checker->isExpected(200));

  EXPECT_FALSE(http_status_checker->isExpected(203));
  EXPECT_TRUE(http_status_checker->isExpected(204));
  EXPECT_FALSE(http_status_checker->isExpected(205));
  EXPECT_FALSE(http_status_checker->isExpected(303));
  EXPECT_TRUE(http_status_checker->isExpected(304));
  EXPECT_FALSE(http_status_checker->isExpected(305));
}

TEST(HttpStatusChecker, Below100) {
  const std::string yaml = R"EOF(
  http_health_check:
    service_name: locations
    path: /healthcheck
    expected_statuses:
      - start: 99
        end: 100
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      HttpStatusChecker::configure(
          parseHealthCheckFromV2Yaml(yaml).http_health_check().expected_statuses(), 200),
      EnvoyException, "Invalid http status range: expecting start >= 100, but found start=99");
}

TEST(HttpStatusChecker, Above599) {
  const std::string yaml = R"EOF(
  http_health_check:
    service_name: locations
    path: /healthchecka
    expected_statuses:
      - start: 600
        end: 601
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      HttpStatusChecker::configure(
          parseHealthCheckFromV2Yaml(yaml).http_health_check().expected_statuses(), 200),
      EnvoyException, "Invalid http status range: expecting end <= 600, but found end=601");
}

TEST(HttpStatusChecker, BadRange) {
  const std::string yaml = R"EOF(
  http_health_check:
    service_name: locations
    path: /healthchecka
    expected_statuses:
      - start: 200
        end: 200
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      HttpStatusChecker::configure(
          parseHealthCheckFromV2Yaml(yaml).http_health_check().expected_statuses(), 200),
      EnvoyException,
      "Invalid http status range: expecting start < end, but found start=200 and end=200");
}

TEST(HttpStatusChecker, BadRange2) {
  const std::string yaml = R"EOF(
  http_health_check:
    service_name: locations
    path: /healthchecka
    expected_statuses:
      - start: 201
        end: 200
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      HttpStatusChecker::configure(
          parseHealthCheckFromV2Yaml(yaml).http_health_check().expected_statuses(), 200),
      EnvoyException,
      "Invalid http status range: expecting start < end, but found start=201 and end=200");
}

} // namespace Upstream
} // namespace Envoy
