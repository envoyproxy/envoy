#pragma once

#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"
#include "common/common/logger.h"

#include "extensions/tracers/xray/tracer.h"

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {

                #define XRAY_TRACER_STATS(COUNTER)              \
                COUNTER(spans_sent)                             \

                struct XRayTracerStats {
                    XRAY_TRACER_STATS(GENERATE_COUNTER_STRUCT)
                };

                class XRayHeader {
                public:
                    XRayHeader(std::string trace_id, std::string parent_id, std::string sample_decision);

                    void fromString(std::string s);

                    XRayHeader() : trace_id_(), parent_id_(), sample_decision_() {}

                    void setTraceId(std::string& val) { trace_id_ = val; }

                    void setParentId(std::string& val) { parent_id_ = val; }

                    void setSampleDecision(std::string& val) { sample_decision_ = val; }

                    std::string& traceId() { return trace_id_; }

                    std::string& parentId() { return parent_id_; }

                    std::string& sampleDecision() { return sample_decision_; }

                    std::string keyFromKeyEqualsValue(std::string pair);

                    std::string valueFromKeyEqualsValue(std::string pair);

                private:
                    std::string trace_id_;
                    std::string parent_id_;
                    std::string sample_decision_;
                };

                class XRaySpan : public Tracing::Span {
                public:
                    /**
                     * Constructor. Wraps a XRay::Span object.
                     *
                     * @param span to be wrapped.
                     */
                    XRaySpan(XRay::Span& span, XRay::Tracer& tracer);

                    /**
                     * Calls XRay::Span::finishSpan() to perform all actions needed to finalize the span.
                     * This function is called by Tracing::HttpTracerUtility::finalizeSpan().
                     */
                    void finishSpan() override;

                    /**
                     * This method sets the operation name on the span.
                     * @param operation the operation name
                     */
                    void setOperation(const std::string& operation) override;

                    /**
                     * This function adds a XRay "string" binary annotation to this span.
                     * In XRay, binary annotations of the type "string" allow arbitrary key-value pairs
                     * to be associated with a span.
                     *
                     * Note that Tracing::HttpTracerUtility::finalizeSpan() makes several calls to this function,
                     * associating several key-value pairs with this span.
                     */
                    void setTag(const std::string& name, const std::string& value) override;

                    void injectContext(Http::HeaderMap& request_headers) override;

                    Tracing::SpanPtr spawnChild(const Tracing::Config&, const std::string& name,
                                                SystemTime start_time) override;

                    void setSampled(bool sampled) override;

                    XRay::Span& span() { return span_; }

                private:
                    XRay::Span span_;
                    XRay::Tracer& tracer_;
                };

                typedef std::unique_ptr<XRaySpan> XRaySpanPtr;

                /**
                * Class for a XRay-specific Driver.
                */
                class Driver : public Tracing::Driver, Logger::Loggable<Logger::Id::tracing> {
                public:
                    /**
                     * Constructor. It adds itself and a newly-created XRay::Tracer object to a thread-local store.
                     */
                    Driver(const envoy::config::trace::v2::XRayConfig& xray_config, Upstream::ClusterManager& cluster_manager, Stats::Store& stats, ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime, const LocalInfo::LocalInfo& localinfo, Runtime::RandomGenerator& random_generator);

                    /**
                     * This function is inherited from the abstract Driver class.
                     *
                     * It starts a new xray span. Depending on the request headers, it can create a root span,
                     * a child span, or a shared-context span.

                     */
                    Tracing::SpanPtr startSpan(const Tracing::Config &, Http::HeaderMap &request_headers, const std::string &,
                              SystemTime start_time, const Tracing::Decision tracing_decision) override;

                    Upstream::ClusterManager &clusterManager() { return cm_; }

                    Upstream::ClusterInfoConstSharedPtr cluster() { return cluster_; }

                    Runtime::Loader &runtime() { return runtime_; }

                    XRayTracerStats& tracerStats() { return tracer_stats_; }

                private:
                    struct TlsTracer : ThreadLocal::ThreadLocalObject {
                        TlsTracer(TracerPtr&& tracer, Driver& driver);

                        TracerPtr tracer_;
                        Driver& driver_;
                    };

                    Upstream::ClusterManager& cm_;
                    Upstream::ClusterInfoConstSharedPtr cluster_;
                    XRayTracerStats tracer_stats_;
                    ThreadLocal::SlotPtr tls_;
                    Runtime::Loader& runtime_;
                    const LocalInfo::LocalInfo& local_info_;
                };

                /**
                  * This is a simple UDP localhost writer for statsd messages.
                  */
                class Writer {
                public:
                    Writer(Network::Address::InstanceConstSharedPtr address);
                    // For testing.
                    Writer() : fd_(-1) {}
                    virtual ~Writer();

                    virtual void write(const std::string& message);
                    // Called in unit test to validate address.
                    int getFdForTests() const { return fd_; };

                private:
                    int fd_;
                };

                class ReporterImpl : public Reporter {
                public:
                  /**
                   * Constructor.
                   *
                   * @param driver XRay to be associated with the reporter.
                   * @param collector_endpoint String representing the XRay Daemon address to be used
                   * when making UDP connection.
                   */
                  ReporterImpl(Writer writer);

                  /**
                   * Implementation of XRay::Reporter::reportSpan().
                   *
                   * When a span is finished will be call.
                   * @param span The span to be sent.
                   */
                  void reportSpan(const Span& span) override;


                  /**
                   *
                   * @param driver XRayDriver to be associated with the reporter.
                   * @param collector_endpoint String representing the XRay Daemon.
                   * This value comes from the XRay-related tracing configuration.
                   *
                   * @return Pointer to the newly-created XRayReporter.
                   */
                  static ReporterPtr NewInstance(Writer writer);

                private:
                    Writer writer_;
                };
            }
        }
    }
}
