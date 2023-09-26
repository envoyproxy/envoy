#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {
namespace {

/*
class ResourceProviderTest : public testing::Test {
public:
  ResourceProviderTest() {
    resource_a_.attributes_.insert(std::pair<std::string, std::string>("key1", "val1"));
    resource_b_.attributes_.insert(std::pair<std::string, std::string>("key2", "val2"));
  }
  NiceMock<Server::Configuration::MockTracerFactoryContext> context_;
  Resource resource_a_;
  Resource resource_b_;
};

*/

class TestSampler: public Sampler {

};

TEST(SamplerFactoryTest, SamplerFactoryTest) {
	EXPECT_TRUE(true);
}


}
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
