#pragma once
#include "test/common/upstream/health_check_fuzz.pb.h"
#include "test/common/upstream/health_checker_impl_test_utils.h"
#include "test/fuzz/common.pb.h"

namespace Envoy {
namespace Upstream {

class HealthCheckFuzz : public HttpHealthCheckerImplTest {
public:
  HealthCheckFuzz();
  void initialize(test::common::upstream::HealthCheckTestCase
                      input); // can pipe in proto configs in either/or of both of these methods

private:
  void respondHeaders(test::fuzz::Headers headers, absl::string_view status,
                      bool respond_on_second_host);
  void streamCreate();
  void allocHealthCheckerFromProto(const envoy::config::core::v3::HealthCheck& config);
  void TestBody() override {}

  void replay(test::common::upstream::HealthCheckTestCase input);

  // Event::SimulatedTimeSystem& time_source_; //is this the correct object?
  bool second_host_;
  Event::SimulatedTimeSystem time_system_;
};

} // namespace Upstream
} // namespace Envoy
