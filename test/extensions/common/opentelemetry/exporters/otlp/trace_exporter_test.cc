#include "source/extensions/common/opentelemetry/exporters/otlp/trace_exporter.h"

#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace OpenTelemetry {
namespace Exporters {
namespace Otlp {
namespace {

// Minimal concrete subclass so that the abstract OtlpTraceExporter can be instantiated in tests.
class TestTraceExporter : public OtlpTraceExporter {
public:
  bool log(const opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest&) override {
    return true;
  }
};

// When resource_spans has no resource set, logExportedSpans emits no log.
TEST(TraceExporterTest, LogExportedSpansNoResource) {
  TestTraceExporter exporter;
  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest request;
  auto* rs = request.add_resource_spans();
  auto* ss = rs->add_scope_spans();
  ss->add_spans()->set_name("test");
  // has_resource() is false — outer branch is not taken, no log emitted.
  EXPECT_LOG_NOT_CONTAINS("debug", "Number of exported spans", exporter.logExportedSpans(request));
}

// When resource is set but scope is not set, logExportedSpans emits no log.
TEST(TraceExporterTest, LogExportedSpansResourceButNoScope) {
  TestTraceExporter exporter;
  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest request;
  auto* rs = request.add_resource_spans();
  rs->mutable_resource(); // sets has_resource() to true
  auto* ss = rs->add_scope_spans();
  ss->add_spans()->set_name("test");
  // has_resource() is true but has_scope() is false — inner branch is not taken, no log emitted.
  EXPECT_LOG_NOT_CONTAINS("debug", "Number of exported spans", exporter.logExportedSpans(request));
}

// When both resource and scope are set, logExportedSpans emits a debug log with the span count.
TEST(TraceExporterTest, LogExportedSpansBothResourceAndScopeSet) {
  TestTraceExporter exporter;
  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest request;
  auto* rs = request.add_resource_spans();
  rs->mutable_resource();
  auto* ss = rs->add_scope_spans();
  ss->mutable_scope();
  ss->add_spans()->set_name("span1");
  ss->add_spans()->set_name("span2");
  EXPECT_LOG_CONTAINS("debug", "Number of exported spans: 2", exporter.logExportedSpans(request));
}

} // namespace
} // namespace Otlp
} // namespace Exporters
} // namespace OpenTelemetry
} // namespace Extensions
} // namespace Envoy
