#include <string>
#include <ctime>
#include <envoy/tracing/http_tracer.h>
#include "extensions/tracers/xray/xray_tracer_impl.h"
#include "extensions/tracers/xray/xray_core_constants.h"
#include "extensions/tracers/xray/sampling.h"

#include "common/network/utility.h"
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {
                void XRayHeader::fromString(std::string s) {
                    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
                    std::vector<std::string> parts;
                    std::string token;
                    std::istringstream tokenStream(s);
                    while (std::getline(tokenStream, token, ';')) {
                        parts.push_back(token);
                    }

                    for (auto it = parts.begin(); it != parts.end() ; it++) {
                        std::string key = keyFromKeyEqualsValue(*it);
                        std::string value = valueFromKeyEqualsValue(*it);
                        if (key == XRayCoreConstants::get().ROOT_KEY) {
                            trace_id_ = value;
                        } else if (key == XRayCoreConstants::get().PARENT_KEY) {
                            parent_id_ = value;
                        } else if (key == XRayCoreConstants::get().SAMPLED_KEY) {
                            sample_decision_ = value;
                        }
                    }
                }

                std::string XRayHeader::keyFromKeyEqualsValue(std::string pair) {
                    size_t key;
                    if ((key = pair.find("=")) != std::string::npos) {
                        return pair.substr(0, key);
                    };
                    return NULL;
                }

                std::string XRayHeader::valueFromKeyEqualsValue(std::string pair) {
                    size_t key;
                    if ((key = pair.find("=")) != std::string::npos) {
                        return pair.substr(key+1, std::string::npos);
                    };
                    return NULL;
                }

                XRaySpan::XRaySpan(XRay::Span& span, XRay::Tracer& tracer) : span_(span), tracer_(tracer) {}

                void XRaySpan::finishSpan() {
                    span_.finish();
                }

                void XRaySpan::setOperation(const std::string& operation) { span_.setOperationName(operation); }

                void XRaySpan::setTag(const std::string& name, const std::string& value) {
                    span_.setTag(name, value);
                }

                void XRaySpan::setSampled(bool sampled) { span_.setSampled(sampled); }

                void XRaySpan::injectContext(Http::HeaderMap& request_headers) { request_headers.size(); }

                void XRaySpan::log(SystemTime timestamp, const std::string& event) {
                    timestamp.time_since_epoch();
                    event.size();
                }

                Tracing::SpanPtr XRaySpan::spawnChild(const Tracing::Config& config, const std::string& name,
                                                        SystemTime start_time) {
                    SpanContext context(span_);
                    return Tracing::SpanPtr{
                            new XRaySpan(*tracer_.startSpan(config, name, start_time, context), tracer_)};
                }

                Driver::TlsTracer::TlsTracer(TracerPtr&& tracer, Driver& driver)
                        : tracer_(std::move(tracer)), driver_(driver) {}


                Driver::Driver(const envoy::config::trace::v2::XRayConfig& xray_config, ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime,
                        const LocalInfo::LocalInfo& local_info, Runtime::RandomGenerator& random_generator)
                    : tls_(tls.allocateSlot()), runtime_(runtime), local_info_(local_info) {

                    std::string daemon_endpoint = XRayCoreConstants::get().DEFAULT_DAEMON_ENDPOINT;
                    if (xray_config.daemon_endpoint().size() > 0) {
                        daemon_endpoint = xray_config.daemon_endpoint();
                    }
                    ENVOY_LOG(info, "send X-Ray generated segments to daemon address on {}", daemon_endpoint);

                    std::string span_name = local_info_.clusterName();
                    if (xray_config.segment_name().size() > 0) {
                        span_name = xray_config.segment_name();
                    }

                    tls_->set([this, daemon_endpoint, span_name, &random_generator](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
                        LocalizedSamplingStrategy localized_sampling_strategy = LocalizedSamplingStrategy("");
                        TracerPtr tracer(new Tracer(span_name, random_generator, localized_sampling_strategy));
                        Network::Address::InstanceConstSharedPtr server_address_ = Network::Utility::parseInternetAddressAndPort(daemon_endpoint, false);

                        std::unique_ptr<Writer> writer(new Writer(server_address_));
                        tracer->setReporter(ReporterImpl::NewInstance(writer));

                        return ThreadLocal::ThreadLocalObjectSharedPtr{new TlsTracer(std::move(tracer), *this)};
                    });
                }

                Tracing::SpanPtr Driver::startSpan(const Tracing::Config &config, Http::HeaderMap &request_headers, const std::string &, SystemTime start_time, const Tracing::Decision tracing_decision) {
                    Tracer& tracer = *tls_->getTyped<TlsTracer>().tracer_;
                    SpanPtr new_xray_span;
                    bool sampled(true);

                    XRayHeader xray_header;
                    if (request_headers.XAmznTraceId()) {
                        std::string trace_header = request_headers.XAmznTraceId()->value().c_str();
                        xray_header.fromString(trace_header);
                    }

                    if (!xray_header.sampleDecision().empty()) {
                        ENVOY_LOG(debug, "use sampling decision from xray header.");
                        sampled = xray_header.sampleDecision() == XRayCoreConstants::get().SAMPLED;
                    } else if (xray_header.sampleDecision().empty()){
                        ENVOY_LOG(debug, "start sampling strategy.");
                        SamplingRequest sampling_request = SamplingRequest(request_headers.Host()->value().c_str(), request_headers.Method()->value().c_str(), request_headers.Path()->value().c_str());
                        sampled = tracer.localizedSamplingStrategy().shouldTrace(sampling_request);
                    } else {
                        ENVOY_LOG(debug, "enable sampling by envoy sampling strategy.");
                        sampled = tracing_decision.traced;
                    }

		            if (!xray_header.traceId().empty() || !xray_header.parentId().empty()) {
                        ENVOY_LOG(debug, "starting generate a segment based on downstream xray header.");
                        uint64_t span_id(0);
                        uint64_t parent_id(0);

                        if (!xray_header.parentId().empty() && !StringUtil::atoull(xray_header.parentId().c_str(), parent_id, 16)) {
                            return Tracing::SpanPtr(new Tracing::NullSpan());
                        }

                        std::string trace_id = xray_header.traceId();
                        SpanContext context(trace_id, span_id, parent_id, sampled);
                        new_xray_span = tracer.startSpan(config, tracer.segmentName(), start_time, context);
                        new_xray_span->setSampled(sampled);

                    } else {
                        // Create a root XRay span. No context was found in the headers.
                        ENVOY_LOG(debug, "starting generate a root segment.");
                        new_xray_span = tracer.startSpan(config, tracer.segmentName(), start_time);
                        new_xray_span->setSampled(sampled);
                    }

                    std::string sample_decision = new_xray_span->sampled() ? XRayCoreConstants::get().SAMPLED : XRayCoreConstants::get().NOT_SAMPLED;
                    std::string xray_header_string = XRayCoreConstants::get().ROOT_PREFIX + new_xray_span->traceId() + "; " +
                                              XRayCoreConstants::get().PARENT_PREFIX + new_xray_span->childSpans()[0].idAsHexString()
                                              + "; " + XRayCoreConstants::get().SAMPLED_PREFIX + sample_decision;
                    request_headers.insertXAmznTraceId().value(xray_header_string);

                    XRaySpan xRaySpan = XRaySpan(*new_xray_span, tracer);
                    XRaySpanPtr active_span(new XRaySpan(*new_xray_span, tracer));

                    return std::move(active_span);
                }

                Writer::Writer(Network::Address::InstanceConstSharedPtr address)
                        : io_handle_(address->socket(Network::Address::SocketType::Datagram)) {
                    ASSERT(io_handle_->fd() != -1);

                    const Api::SysCallIntResult result = address->connect(io_handle_->fd());
                    ASSERT(result.rc_ != -1);
                }

                Writer::~Writer() {
                    if (io_handle_->isOpen()) {
                        RELEASE_ASSERT(io_handle_->close().err_ == nullptr, "");
                    }
                }

                void Writer::write(const std::string& message) {
                    ::send(io_handle_->fd(), message.c_str(), message.size(), MSG_DONTWAIT);
                }

                ReporterImpl::ReporterImpl(std::unique_ptr<Writer>& writer) : writer_(std::move(writer)) {}

                ReporterPtr ReporterImpl::NewInstance(std::unique_ptr<Writer>& writer) {
                    return ReporterPtr(new ReporterImpl(writer));
                }

                void ReporterImpl::reportSpan(const Span& span) {
                    // Only send out the sampled segment to X-Ray daemon
                    if (span.sampled()) {
                        std::vector<Span> span_buffer_;
                        span_buffer_.push_back(std::move(span));
                        writer_->write(span_buffer_[0].toJson());
                    }
                }
            } // namespace XRay
        } // namespace Tracers
    } // namespace Extensions
} // namespace Envoy
