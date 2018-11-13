#include <string>
#include <iostream>
#include <ctime>
#include "extensions/tracers/xray/xray_tracer_impl.h"
#include "extensions/tracers/xray/xray_core_constants.h"

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
                XRayHeader::XRayHeader(std::string trace_id, std::string parent_id, std::string sample_decision) :
                trace_id_(trace_id), parent_id_(parent_id), sample_decision_(sample_decision) {}

                void XRayHeader::fromString(std::string s) {
                    std::cout << "enter from string" << std::endl;
                    std::cout << "before erase white space " << s << std::endl;
                    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());

                    std::cout << "erase white space " << s << std::endl;
                    std::vector<std::string> parts;
                    std::string token;
                    std::istringstream tokenStream(s);
                    while (std::getline(tokenStream, token, ';')) {
                        std::cout << token << std::endl;
                        parts.push_back(token);
                    }

                    for (auto it = parts.begin(); it != parts.end() ; it++) {
                        std::string key = keyFromKeyEqualsValue(*it);
                        std::string value = valueFromKeyEqualsValue(*it);
                        if (key == XRayCoreConstants::get().ROOT_KEY) {
                            std::cout << value << std::endl;
                            trace_id_ = value;
                        } else if (key == XRayCoreConstants::get().PARENT_KEY) {
                            std::cout << value << std::endl;
                            parent_id_ = value;
                        } else if (key == XRayCoreConstants::get().SAMPLED_KEY) {
                            std::cout << value << std::endl;
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
                    std::cout << "called finishSpan" << std::endl;
                    span_.finish();
                }

                void XRaySpan::setOperation(const std::string& operation) { span_.setName(operation); }

                void XRaySpan::setTag(const std::string& name, const std::string& value) {
                    span_.setTag(name, value);
                }

                void XRaySpan::injectContext(Http::HeaderMap& request_headers) {
                    // Set the trace-id and span-id headers properly, based on the newly-created span structure.
                    request_headers.size();
                }

                void XRaySpan::setSampled(bool sampled) { span_.setSampled(sampled); }

                Tracing::SpanPtr XRaySpan::spawnChild(const Tracing::Config& config, const std::string& name,
                                                        SystemTime start_time) {
		            std::cout << "enter spawnchild" << std::endl;
                    SpanContext context(span_);
                    return Tracing::SpanPtr{
                            new XRaySpan(*tracer_.startSpan(config, name, start_time, context), tracer_)};
                }

                Driver::TlsTracer::TlsTracer(TracerPtr&& tracer, Driver& driver)
                        : tracer_(std::move(tracer)), driver_(driver) {}


                Driver::Driver(const envoy::config::trace::v2::XRayConfig& xray_config, Upstream::ClusterManager& cluster_manager, Stats::Store& stats, ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime,
                               const LocalInfo::LocalInfo& local_info, Runtime::RandomGenerator& random_generator)
                    : cm_(cluster_manager), tracer_stats_{XRAY_TRACER_STATS(POOL_COUNTER_PREFIX(stats, "tracing.xray."))}, tls_(tls.allocateSlot()), runtime_(runtime), local_info_(local_info) {

                    Upstream::ThreadLocalCluster* cluster = cm_.get(xray_config.collector_cluster());
                    if (!cluster) {
                        throw EnvoyException(fmt::format("{} collector cluster is not defined on cluster manager level",
                                                         xray_config.collector_cluster()));
                    }
                    cluster_ = cluster->info();

                    random_generator.uuid();

                    std::string collector_endpoint = XRayCoreConstants::get().DEFAULT_DAEMON_ENDPOINT;
                    if (xray_config.collector_endpoint().size() > 0) {
                        collector_endpoint = xray_config.collector_endpoint();
                    }
		            std::cout << "send to daemon on " << collector_endpoint << std::endl;

                    tls_->set([this, collector_endpoint, &random_generator](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
                        TracerPtr tracer(new Tracer(local_info_.clusterName(), random_generator));

                        Network::Address::InstanceConstSharedPtr server_address_ = Network::Utility::parseInternetAddressAndPort(collector_endpoint, false);

                        Writer wt = Writer(server_address_);
                        tracer->setReporter(ReporterImpl::NewInstance(wt));

                        return ThreadLocal::ThreadLocalObjectSharedPtr{new TlsTracer(std::move(tracer), *this)};
                    });
                }


                Tracing::SpanPtr Driver::startSpan(const Tracing::Config &config, Http::HeaderMap &request_headers, const std::string &, SystemTime start_time, const Tracing::Decision tracing_decision) {
                    Tracer& tracer = *tls_->getTyped<TlsTracer>().tracer_;
                    SpanPtr new_xray_span;
                    bool sampled(true);

                    XRayHeader xrayHeader;
                    if (request_headers.XAmznTraceId()) {
                        std::string traceHeader = request_headers.XAmznTraceId()->value().c_str();
                        xrayHeader.fromString(traceHeader);
                        if (!xrayHeader.sampleDecision().empty()) {
                            sampled = xrayHeader.sampleDecision() == XRayCoreConstants::get().SAMPLED;
                        } else {
                            sampled = tracing_decision.traced;
                        }
                    }

                    std::cout << "Hello from xray service!" << "\n";
                    std::cout << "Get sampling decision for upstream: " << tracing_decision.traced << "\n";
                    std::cout << "Trace Header: " << xrayHeader.traceId() << ", " << xrayHeader.parentId() << ", " << xrayHeader.sampleDecision() << std::endl;

		            if (!xrayHeader.traceId().empty() || !xrayHeader.parentId().empty()) {
			            std::cout << "create a span based on downstream traceid and parentid" << std::endl;
                        uint64_t span_id(0);
                        uint64_t parent_id(0);

                        if (!xrayHeader.parentId().empty() && !StringUtil::atoul(xrayHeader.parentId().c_str(), parent_id, 16)) {
                            std::cout << "something went wrong in parse parent id" << std::endl;
                            return Tracing::SpanPtr(new Tracing::NullSpan());
                        }

                        std::string trace_id = xrayHeader.traceId();
                        SpanContext context(trace_id, span_id, parent_id, sampled);
                        new_xray_span = tracer.startSpan(config, request_headers.Host()->value().c_str(), start_time, context);

                        std::string s = new_xray_span->sampled() ? XRayCoreConstants::get().SAMPLED : XRayCoreConstants::get().NOT_SAMPLED;
                        std::string xray_header = XRayCoreConstants::get().ROOT_PREFIX + new_xray_span->traceId() + "; " +
                                                  XRayCoreConstants::get().PARENT_PREFIX + new_xray_span->childSpans()[0].idAsHexString()
                                                  + "; " + XRayCoreConstants::get().SAMPLED_PREFIX + s;
                        std::cout << "This is the trace header for upstream: " << xray_header << std::endl;
                        request_headers.insertXAmznTraceId().value(xray_header);

                    } else {
                        // Create a root XRay span. No context was found in the headers.
                        std::cout << "start create root span." << "\n";
                        new_xray_span = tracer.startSpan(config, request_headers.Host()->value().c_str(), start_time);
                        new_xray_span->setSampled(sampled);

                        std::string s = new_xray_span->sampled() ? XRayCoreConstants::get().SAMPLED : XRayCoreConstants::get().NOT_SAMPLED;
                        std::string xray_header = XRayCoreConstants::get().ROOT_PREFIX + new_xray_span->traceId() + "; " +
                                XRayCoreConstants::get().PARENT_PREFIX + new_xray_span->childSpans()[0].idAsHexString()
                                                  + "; " + XRayCoreConstants::get().SAMPLED_PREFIX + s;
                        std::cout << "This is the trace header for upstream: " << xray_header << std::endl;
                        request_headers.insertXAmznTraceId().value(xray_header);
                    }

                    XRaySpan xRaySpan = XRaySpan(*new_xray_span, tracer);
                    XRaySpanPtr active_span(new XRaySpan(*new_xray_span, tracer));

                    return std::move(active_span);
                }

                Writer::Writer(Network::Address::InstanceConstSharedPtr address) {
                    fd_ = address->socket(Network::Address::SocketType::Datagram);
                    ASSERT(fd_ != -1);

                    const Api::SysCallIntResult result = address->connect(fd_);
                    ASSERT(result.rc_ != -1);
                }

                Writer::~Writer() {
//                    if (fd_ != -1) {
//                        RELEASE_ASSERT(close(fd_) == 0, "");
//                    }
                }

                void Writer::write(const std::string& message) {
                    ::send(fd_, message.c_str(), message.size(), MSG_DONTWAIT);
                }

                ReporterImpl::ReporterImpl(Writer writer) : writer_(writer) {}

                ReporterPtr ReporterImpl::NewInstance(Writer writer) {
                    return ReporterPtr(new ReporterImpl(writer));
                }

                void ReporterImpl::reportSpan(const Span& span) {
                    std::cout << "report span been called" << std::endl;
                    std::vector<Span> span_buffer_;
                    span_buffer_.push_back(std::move(span));
                    writer_.write(span_buffer_[0].toJson());
                }
            }
        }
    }
}
