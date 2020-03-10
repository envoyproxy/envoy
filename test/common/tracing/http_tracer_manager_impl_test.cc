#include "common/tracing/http_tracer_config_impl.h"
#include "common/tracing/http_tracer_impl.h"
#include "common/tracing/http_tracer_manager_impl.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::NotNull;
using testing::WhenDynamicCastTo;

namespace Envoy {
namespace Tracing {
namespace {

class HttpTracerManagerImplTest : public testing::Test {
public:
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  HttpTracerManagerImpl http_tracer_manager_{
      std::make_unique<TracerFactoryContextImpl>(server_factory_context_, ProtobufMessage::getStrictValidationVisitor())};
};

TEST_F(HttpTracerManagerImplTest, ShouldReturnHttpNullTracerWhenNoTracingProviderHasBeenConfigured) {
  auto http_tracer = http_tracer_manager_.getOrCreateHttpTracer(nullptr);

  // Should return a null object (Tracing::HttpNullTracer) rather than nullptr.
  EXPECT_THAT(http_tracer.get(), WhenDynamicCastTo<Tracing::HttpNullTracer*>(NotNull()));
}

} // namespace
} // namespace Tracing
} // namespace Envoy
