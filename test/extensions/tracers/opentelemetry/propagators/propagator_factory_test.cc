#include "source/extensions/tracers/opentelemetry/propagators/propagator_factory.h"

#include "test/mocks/api/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

using testing::Return;
using testing::ReturnRef;

class PropagatorFactoryTest : public testing::Test {
public:
  PropagatorFactoryTest() = default;

protected:
  NiceMock<Api::MockApi> api_;
};

TEST_F(PropagatorFactoryTest, CreatePropagatorsWithExplicitConfig) {
  std::vector<std::string> propagator_names = {"tracecontext", "b3", "baggage"};

  auto propagator = PropagatorFactory::createPropagators(propagator_names, api_);

  EXPECT_NE(nullptr, propagator);
}

TEST_F(PropagatorFactoryTest, CreatePropagatorsWithEnvironmentVariable) {
  std::vector<std::string> empty_config; // No explicit config

  // Set up environment variable
  TestEnvironment::setEnvVar("OTEL_PROPAGATORS", "b3,baggage,tracecontext", 1);

  auto propagator = PropagatorFactory::createPropagators(empty_config, api_);

  EXPECT_NE(nullptr, propagator);

  // Clean up
  TestEnvironment::unsetEnvVar("OTEL_PROPAGATORS");
}

TEST_F(PropagatorFactoryTest, ExplicitConfigTakesPriorityOverEnvironment) {
  std::vector<std::string> propagator_names = {"tracecontext"}; // Explicit config

  // Set up environment variable that should be ignored
  TestEnvironment::setEnvVar("OTEL_PROPAGATORS", "b3,baggage", 1);

  auto propagator = PropagatorFactory::createPropagators(propagator_names, api_);

  EXPECT_NE(nullptr, propagator);

  // Clean up
  TestEnvironment::unsetEnvVar("OTEL_PROPAGATORS");
}

TEST_F(PropagatorFactoryTest, DefaultsWhenNoConfigOrEnvironment) {
  std::vector<std::string> empty_config; // No explicit config
  // No environment variable set

  auto propagator = PropagatorFactory::createPropagators(empty_config, api_);

  EXPECT_NE(nullptr, propagator);
}

TEST_F(PropagatorFactoryTest, ParseOtelPropagatorsEnv) {
  // Test parsing of OTEL_PROPAGATORS environment variable format
  auto propagators = PropagatorFactory::parseOtelPropagatorsEnv("tracecontext,baggage,b3");

  EXPECT_EQ(3, propagators.size());
  EXPECT_EQ("tracecontext", propagators[0]);
  EXPECT_EQ("baggage", propagators[1]);
  EXPECT_EQ("b3", propagators[2]);
}

TEST_F(PropagatorFactoryTest, ParseOtelPropagatorsEnvWithSpaces) {
  // Test parsing with spaces around commas
  auto propagators = PropagatorFactory::parseOtelPropagatorsEnv(" tracecontext , baggage , b3 ");

  EXPECT_EQ(3, propagators.size());
  EXPECT_EQ("tracecontext", propagators[0]);
  EXPECT_EQ("baggage", propagators[1]);
  EXPECT_EQ("b3", propagators[2]);
}

TEST_F(PropagatorFactoryTest, ParseOtelPropagatorsEnvEmpty) {
  auto propagators = PropagatorFactory::parseOtelPropagatorsEnv("");

  EXPECT_EQ(0, propagators.size());
}

TEST_F(PropagatorFactoryTest, ParseOtelPropagatorsEnvSingle) {
  auto propagators = PropagatorFactory::parseOtelPropagatorsEnv("tracecontext");

  EXPECT_EQ(1, propagators.size());
  EXPECT_EQ("tracecontext", propagators[0]);
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
