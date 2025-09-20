#include "source/extensions/propagators/opentelemetry/propagator_factory.h"

#include "test/mocks/api/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
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

TEST_F(PropagatorFactoryTest, CreatePropagatorsWithoutApiDefaults) {
  std::vector<std::string> propagator_names = {"tracecontext", "b3"};

  auto propagator = PropagatorFactory::createPropagators(propagator_names);

  EXPECT_NE(nullptr, propagator);
}

TEST_F(PropagatorFactoryTest, CreateDefaultPropagators) {
  auto propagator = PropagatorFactory::createDefaultPropagators();

  EXPECT_NE(nullptr, propagator);
}

TEST_F(PropagatorFactoryTest, ParseOtelPropagatorsEnv) {
  std::string env_value = "tracecontext,b3,baggage";

  auto propagators = PropagatorFactory::parseOtelPropagatorsEnv(env_value);

  EXPECT_EQ(3, propagators.size());
  EXPECT_EQ("tracecontext", propagators[0]);
  EXPECT_EQ("b3", propagators[1]);
  EXPECT_EQ("baggage", propagators[2]);
}

TEST_F(PropagatorFactoryTest, ParseOtelPropagatorsEnvWithSpaces) {
  std::string env_value = " tracecontext , b3 , baggage ";

  auto propagators = PropagatorFactory::parseOtelPropagatorsEnv(env_value);

  EXPECT_EQ(3, propagators.size());
  EXPECT_EQ("tracecontext", propagators[0]);
  EXPECT_EQ("b3", propagators[1]);
  EXPECT_EQ("baggage", propagators[2]);
}

TEST_F(PropagatorFactoryTest, ParseOtelPropagatorsEnvEmpty) {
  std::string env_value = "";

  auto propagators = PropagatorFactory::parseOtelPropagatorsEnv(env_value);

  EXPECT_EQ(0, propagators.size());
}

TEST_F(PropagatorFactoryTest, CreatePropagatorsWithUnknownName) {
  std::vector<std::string> propagator_names = {"unknown", "tracecontext"};

  auto propagator = PropagatorFactory::createPropagators(propagator_names);

  EXPECT_NE(nullptr, propagator);
}

TEST_F(PropagatorFactoryTest, CreatePropagatorsWithEmptyNames) {
  std::vector<std::string> propagator_names = {};

  auto propagator = PropagatorFactory::createPropagators(propagator_names);

  EXPECT_NE(nullptr, propagator);
}

} // namespace OpenTelemetry
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
