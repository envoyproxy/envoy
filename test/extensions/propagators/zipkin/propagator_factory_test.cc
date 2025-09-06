#include "source/extensions/propagators/zipkin/propagator_factory.h"

#include "source/extensions/tracers/zipkin/span_context.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace Zipkin {
namespace {

class PropagatorFactoryTest : public testing::Test {
public:
  void SetUp() override {}
};

TEST_F(PropagatorFactoryTest, CreateDefaultPropagators) {
  auto composite = PropagatorFactory::createDefaultPropagators();
  ASSERT_NE(composite, nullptr);

  // Test that default includes B3 propagator
  Http::TestRequestHeaderMapImpl headers{{"x-b3-traceid", "0000000000000001"},
                                         {"x-b3-spanid", "0000000000000002"},
                                         {"x-b3-sampled", "1"}};
  Tracing::TestTraceContextImpl trace_context{headers};

  EXPECT_TRUE(composite->propagationHeaderPresent(trace_context));

  auto result = composite->extract(trace_context);
  EXPECT_TRUE(result.ok());
  EXPECT_TRUE(result.value().sampled());
}

TEST_F(PropagatorFactoryTest, CreatePropagators) {
  std::vector<std::string> propagator_names = {"b3", "tracecontext"};
  auto composite = PropagatorFactory::createPropagators(propagator_names);
  ASSERT_NE(composite, nullptr);

  // Test B3 propagation
  Http::TestRequestHeaderMapImpl b3_headers{{"x-b3-traceid", "0000000000000001"},
                                            {"x-b3-spanid", "0000000000000002"},
                                            {"x-b3-sampled", "1"}};
  Tracing::TestTraceContextImpl b3_context{b3_headers};

  EXPECT_TRUE(composite->propagationHeaderPresent(b3_context));
  auto b3_result = composite->extract(b3_context);
  EXPECT_TRUE(b3_result.ok());

  // Test W3C propagation
  Http::TestRequestHeaderMapImpl w3c_headers{
      {"traceparent", "00-00000000000000010000000000000002-0000000000000003-01"}};
  Tracing::TestTraceContextImpl w3c_context{w3c_headers};

  EXPECT_TRUE(composite->propagationHeaderPresent(w3c_context));
  auto w3c_result = composite->extract(w3c_context);
  EXPECT_TRUE(w3c_result.ok());
}

TEST_F(PropagatorFactoryTest, CreatePropagatorsEmptyList) {
  std::vector<std::string> empty_list;
  auto composite = PropagatorFactory::createPropagators(empty_list);
  ASSERT_NE(composite, nullptr);

  // Should default to B3
  Http::TestRequestHeaderMapImpl headers{{"x-b3-traceid", "0000000000000001"},
                                         {"x-b3-spanid", "0000000000000002"},
                                         {"x-b3-sampled", "1"}};
  Tracing::TestTraceContextImpl trace_context{headers};

  EXPECT_TRUE(composite->propagationHeaderPresent(trace_context));
}

TEST_F(PropagatorFactoryTest, CreatePropagatorsInvalidName) {
  std::vector<std::string> invalid_names = {"invalid", "unknown"};
  auto composite = PropagatorFactory::createPropagators(invalid_names);
  ASSERT_NE(composite, nullptr);

  // Should default to B3 when no valid propagators found
  Http::TestRequestHeaderMapImpl headers{{"x-b3-traceid", "0000000000000001"},
                                         {"x-b3-spanid", "0000000000000002"},
                                         {"x-b3-sampled", "1"}};
  Tracing::TestTraceContextImpl trace_context{headers};

  EXPECT_TRUE(composite->propagationHeaderPresent(trace_context));
}

TEST_F(PropagatorFactoryTest, CreateSinglePropagator) {
  std::vector<std::string> b3_only = {"b3"};
  auto composite = PropagatorFactory::createPropagators(b3_only);
  ASSERT_NE(composite, nullptr);

  // Test B3 propagation works
  Http::TestRequestHeaderMapImpl b3_headers{{"x-b3-traceid", "0000000000000001"},
                                            {"x-b3-spanid", "0000000000000002"},
                                            {"x-b3-sampled", "1"}};
  Tracing::TestTraceContextImpl b3_context{b3_headers};

  EXPECT_TRUE(composite->propagationHeaderPresent(b3_context));

  // Test W3C propagation does not work (not configured)
  Http::TestRequestHeaderMapImpl w3c_headers{
      {"traceparent", "00-00000000000000010000000000000002-0000000000000003-01"}};
  Tracing::TestTraceContextImpl w3c_context{w3c_headers};

  EXPECT_FALSE(composite->propagationHeaderPresent(w3c_context));
}

} // namespace
} // namespace Zipkin
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
