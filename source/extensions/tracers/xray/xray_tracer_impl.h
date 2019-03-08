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
                /**
                 * Class for XRay Header.
                 */
                class XRayHeader {
                public:
                    /**
                     * Constructor. A X-Ray Header contains trace id, parent id and sampling decision.
                     */
                    XRayHeader(std::string trace_id, std::string parent_id, std::string sample_decision) :
                            trace_id_(trace_id), parent_id_(parent_id), sample_decision_(sample_decision) {}

                    /**
                     * Default constructor. Creates an empty XRayHeader.
                     */
                    XRayHeader() : trace_id_(), parent_id_(), sample_decision_() {}

                    /**
                    * Get XRay header values from a given string.
                    */
                    void fromString(std::string s);

                    /**
                     * Set trace id.
                     */
                    void setTraceId(std::string& val) { trace_id_ = val; }

                    /**
                     * Set parent id.
                     */
                    void setParentId(std::string& val) { parent_id_ = val; }

                    /**
                     * Set sampling decision.
                     */
                    void setSampleDecision(std::string& val) { sample_decision_ = val; }

                    /**
                     * @return the tracer id.
                     */
                    std::string& traceId() { return trace_id_; }

                    /**
                     * @return the parent id.
                     */
                    std::string& parentId() { return parent_id_; }

                    /**
                     * @return the sampling decision.
                     */
                    std::string& sampleDecision() { return sample_decision_; }

                    /**
                     * Get key from key=value pattern.
                     */
                    std::string keyFromKeyEqualsValue(std::string pair);

                    /**
                     * Get value from key=value pattern.
                     */
                    std::string valueFromKeyEqualsValue(std::string pair);

                private:
                    std::string trace_id_;
                    std::string parent_id_;
                    std::string sample_decision_;
                };

                /**
                 * Class for XRay Span.
                 */
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
                     * This function adds a XRay "string" type key-value pair annotation to this span.
                     * Note that Tracing::HttpTracerUtility::finalizeSpan() makes several calls to this function,
                     * associating several key-value pairs with this span.
                     */
                    void setTag(const std::string& name, const std::string& value) override;

                    /**
                     * Deprecated this function for XRay implementation.
                     * This function injects headers into HeaderMap for context propagation.
                     */
                    void injectContext(Http::HeaderMap& request_headers) override;

                    /**
                     * Deprecated this function for XRay implementation.
                     */
                    Tracing::SpanPtr spawnChild(const Tracing::Config&, const std::string& name,
                                                SystemTime start_time) override;

                    /**
                     * This function sets sampling decision.
                     */
                    void setSampled(bool sampled) override;

                    /**
                     * @return a reference to the XRay::Span object.
                     */
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
                    Driver(const envoy::config::trace::v2::XRayConfig& xray_config, ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime,
                           const LocalInfo::LocalInfo& localinfo, Runtime::RandomGenerator& random_generator);

                    /**
                     * This function is inherited from the abstract Driver class.
                     *
                     * It starts a new xray span. Depending on the request headers, it can create a root span or a shared-context span.
                     */
                    Tracing::SpanPtr startSpan(const Tracing::Config &, Http::HeaderMap &request_headers, const std::string &,
                              SystemTime start_time, const Tracing::Decision tracing_decision) override;

                    Runtime::Loader &runtime() { return runtime_; }

                private:
                    /**
                     * Thread-local store containing XRayDriver and XRay::Tracer objects.
                     */
                    struct TlsTracer : ThreadLocal::ThreadLocalObject {
                        TlsTracer(TracerPtr&& tracer, Driver& driver);

                        TracerPtr tracer_;
                        Driver& driver_;
                    };

                    ThreadLocal::SlotPtr tls_;
                    Runtime::Loader& runtime_;
                    const LocalInfo::LocalInfo& local_info_;
                };

                /**
                  * This is a simple UDP localhost writer for sending spans to X-Ray Daemon.
                  */
                class Writer {
                public:
                    /**
                     * The writer will take X-Ray daemon address and write segments into UDP socket.
                     * @param address The X-Ray daemon address
                     */
                    Writer(Network::Address::InstanceConstSharedPtr address);

                    Writer() : fd_(-1) {}

                    /**
                     * Destructor.
                     */
                    virtual ~Writer();

                    /**
                     * Writes segment json doc into UDP socket.
                     * @param message
                     */
                    virtual void write(const std::string& message);

                private:
                    int fd_;
                };

                /**
                 * This class derives from the abstract XRay::Reporter.
                 */
                class ReporterImpl : public Reporter {
                public:
                  /**
                   * Constructor.
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
                   * @return Pointer to the newly-created XRayReporter.
                   */
                  static ReporterPtr NewInstance(Writer writer);

                private:
                    Writer writer_;
                };
            } // namespace XRay
        } // namespace Tracers
    } // namespace Extensions
} // namespace Envoy
