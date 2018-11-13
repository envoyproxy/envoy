#include "extensions/tracers/xray/xray_core_types.h"

#include "common/common/utility.h"

#include "extensions/tracers/xray/span_context.h"
#include "extensions/tracers/xray/util.h"
#include "extensions/tracers/xray/xray_core_constants.h"
#include "extensions/tracers/xray/xray_json_field_names.h"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <ctime>
#include <string>

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {

                const std::string Span::EMPTY_HEX_STRING_ = "0000000000000000";
                const std::string Span::VERSION_ = "1";
                const std::string Span::FORMAT_ = "json";

                BinaryAnnotation::BinaryAnnotation(const BinaryAnnotation& ann) {
                    key_ = ann.key();
                    value_ = ann.value();
                    annotation_type_ = ann.annotationType();
                }

                BinaryAnnotation& BinaryAnnotation::operator=(const BinaryAnnotation& ann) {
                    key_ = ann.key();
                    value_ = ann.value();
                    annotation_type_ = ann.annotationType();

                    return *this;
                }

                const std::string BinaryAnnotation::toJson() {
                    rapidjson::StringBuffer s;
                    rapidjson::Writer<rapidjson::StringBuffer> writer(s);
                    writer.StartObject();
                    writer.Key(key_.c_str());
                    writer.String(value_.c_str());
                    writer.EndObject();

                    std::string json_string = s.GetString();

                    return json_string;
                }

                ChildSpan::ChildSpan(const ChildSpan &child) {
                    name_ = child.name();
                    id_ = child.id();
                    binary_annotations_ = child.binaryAnnotations();
                    start_time_ = child.startTime();
                }

                const std::string ChildSpan::toJson() {
                    rapidjson::StringBuffer s;
                    rapidjson::Writer<rapidjson::StringBuffer> writer(s);
                    writer.StartObject();
                    writer.Key(XRayJsonFieldNames::get().SPAN_ID.c_str());
                    writer.String(idAsHexString().c_str());
                    writer.Key(XRayJsonFieldNames::get().SPAN_START_TIME.c_str());
                    writer.Double(start_time_);
                    writer.Key(XRayJsonFieldNames::get().SPAN_END_TIME.c_str());
                    const double stop_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    writer.Double(stop_time);
                    writer.Key(XRayJsonFieldNames::get().SPAN_NAMESPACE.c_str());
                    writer.String(XRayJsonFieldNames::get().SPAN_REMOTE.c_str());

                    std::vector<BinaryAnnotation> http_request_annotation_json_vector;
                    std::vector<BinaryAnnotation> http_response_annotation_json_vector;

                    writer.Key(XRayJsonFieldNames::get().SPAN_NAME.c_str());
                    std::string span_name;

                    for (auto it = binary_annotations_.begin(); it != binary_annotations_.end(); it++) {
                        if (it->key() == XRayCoreConstants::get().HTTP_URL) {
                            it->setKey(XRayJsonFieldNames::get().SPAN_URL);
                            http_request_annotation_json_vector.push_back(*it);
                        } else if (it->key() == XRayCoreConstants::get().HTTP_METHOD) {
                            it->setKey(XRayJsonFieldNames::get().SPAN_METHOD);
                            http_request_annotation_json_vector.push_back(*it);
                        } else if (it->key() == XRayCoreConstants::get().HTTP_USER_AGENT) {
                            it->setKey(XRayJsonFieldNames::get().SPAN_USER_AGENT);
                            http_request_annotation_json_vector.push_back(*it);
                        } else if (it->key() == XRayCoreConstants::get().HTTP_STATUS_CODE) {
                            it->setKey(XRayJsonFieldNames::get().SPAN_STATUS);
                            http_response_annotation_json_vector.push_back(*it);
                        } else if (it->key() == XRayCoreConstants::get().UPSTREAM_CLUSTER) {
                            span_name = it->value();
                        }
                    }

                    if (span_name != "-" && !span_name.empty()) {
                        writer.String(span_name.c_str());
                    } else {
                        writer.String(name_.c_str());
                    }

                    if (http_request_annotation_json_vector.size() != 0 || http_response_annotation_json_vector.size() != 0) {
                        writer.Key(XRayJsonFieldNames::get().SPAN_HTTP_ANNOTATIONS.c_str());
                        writer.StartObject();

                        if (http_request_annotation_json_vector.size() != 0) {
                            writer.Key(XRayJsonFieldNames::get().SPAN_REQUEST.c_str());
                            writer.StartObject();
                            for (auto it = http_request_annotation_json_vector.begin(); it != http_request_annotation_json_vector.end(); it++) {
                                writer.Key(it->key().c_str());
                                writer.String(it->value().c_str());
                            }
                            writer.EndObject();
                        }

                        if (http_response_annotation_json_vector.size() != 0) {
                            writer.Key(XRayJsonFieldNames::get().SPAN_RESPONSE.c_str());
                            writer.StartObject();
                            for (auto it = http_response_annotation_json_vector.begin(); it != http_response_annotation_json_vector.end(); it++) {
                                writer.Key(it->key().c_str());
                                writer.Int(std::stoi(it->value()));
                            }
                            writer.EndObject();
                        }

                        // end http
                        writer.EndObject();
                    }

                    writer.EndObject();

                    std::string json_string = s.GetString();

                    return json_string;
                }

                Span::Span(const Span &span) {
                    trace_id_ = span.traceId();
                    name_ = span.name();
                    id_ = span.id();
                    if (span.isSetParentId()) {
                        parent_id_ = span.parentId();
                    }
                    debug_ = span.debug();
                    sampled_ = span.sampled();
                    binary_annotations_ = span.binaryAnnotations();
                    if (span.isSetTimestamp()) {
                        timestamp_ = span.timestamp();
                    }
                    if (span.isSetDuration()) {
                        duration_ = span.duration();
                    }
                    start_time_ = span.startTime();
                    tracer_ = span.tracer();
                    child_span_ = span.childSpans();
                }

                const std::string Span::toJson() {
                    // Header information contains Tracing Daemon Wire Protocol in order to send span to daemon.
                    rapidjson::StringBuffer h;
                    rapidjson::Writer<rapidjson::StringBuffer> writer_h(h);
                    writer_h.StartObject();
                    writer_h.Key(XRayJsonFieldNames::get().HEADER_FORMAT.c_str());
                    writer_h.String(FORMAT_.c_str());
                    writer_h.Key(XRayJsonFieldNames::get().HEADER_VERSION.c_str());
                    writer_h.Int(std::stoi(VERSION_));
                    writer_h.EndObject();
                    std::string header_json_string = h.GetString();

                    rapidjson::StringBuffer s;
                    rapidjson::Writer<rapidjson::StringBuffer> writer(s);
                    writer.StartObject();
                    writer.Key(XRayJsonFieldNames::get().SPAN_TRACE_ID.c_str());
                    writer.String(trace_id_.c_str());
                    writer.Key(XRayJsonFieldNames::get().SPAN_ID.c_str());
                    writer.String(idAsHexString().c_str());

                    if (parent_id_ && parent_id_.value()) {
                        writer.Key(XRayJsonFieldNames::get().SPAN_PARENT_ID.c_str());
                        writer.String(Hex::uint64ToHex(parent_id_.value()).c_str());
                    }

                    writer.Key(XRayJsonFieldNames::get().SPAN_START_TIME.c_str());
                    writer.Double(start_time_);

                    writer.Key(XRayJsonFieldNames::get().SPAN_END_TIME.c_str());
		            const double stop_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    writer.Double(stop_time);

                    writer.Key(XRayJsonFieldNames::get().SPAN_ORIGIN.c_str());
                    writer.String(XRayJsonFieldNames::get().SPAN_ORIGIN_VALUE.c_str());

                    std::vector<BinaryAnnotation> http_request_annotation_json_vector;
                    std::vector<BinaryAnnotation> http_response_annotation_json_vector;

                    writer.Key(XRayJsonFieldNames::get().SPAN_NAME.c_str());
                    std::string span_name;

                    for (auto it = binary_annotations_.begin(); it != binary_annotations_.end(); it++) {
                        if (it->key() == XRayCoreConstants::get().HTTP_URL) {
                            it->setKey(XRayJsonFieldNames::get().SPAN_URL);
                            http_request_annotation_json_vector.push_back(*it);
                        } else if (it->key() == XRayCoreConstants::get().HTTP_METHOD) {
                            it->setKey(XRayJsonFieldNames::get().SPAN_METHOD);
                            http_request_annotation_json_vector.push_back(*it);
                        } else if (it->key() == XRayCoreConstants::get().HTTP_USER_AGENT) {
                            it->setKey(XRayJsonFieldNames::get().SPAN_USER_AGENT);
                            http_request_annotation_json_vector.push_back(*it);
                        } else if (it->key() == XRayCoreConstants::get().HTTP_STATUS_CODE) {
                            it->setKey(XRayJsonFieldNames::get().SPAN_STATUS);
                            http_response_annotation_json_vector.push_back(*it);
                        } else if (it->key() == XRayCoreConstants::get().UPSTREAM_CLUSTER) {
                            span_name = it->value();
                        }
                    }

                    if (span_name != "-" && !span_name.empty()) {
                        writer.String(span_name.c_str());
                    } else {
                        writer.String(name_.c_str());
                    }

                    if (http_request_annotation_json_vector.size() != 0 || http_response_annotation_json_vector.size() != 0) {
                        writer.Key(XRayJsonFieldNames::get().SPAN_HTTP_ANNOTATIONS.c_str());
                        writer.StartObject();

                        if (http_request_annotation_json_vector.size() != 0) {
                            writer.Key(XRayJsonFieldNames::get().SPAN_REQUEST.c_str());
                            writer.StartObject();
                            for (auto it = http_request_annotation_json_vector.begin(); it != http_request_annotation_json_vector.end(); it++) {
                                writer.Key(it->key().c_str());
                                writer.String(it->value().c_str());
                            }
                            writer.EndObject();
                        }

                        if (http_response_annotation_json_vector.size() != 0) {
                            writer.Key(XRayJsonFieldNames::get().SPAN_RESPONSE.c_str());
                            writer.StartObject();
                            for (auto it = http_response_annotation_json_vector.begin(); it != http_response_annotation_json_vector.end(); it++) {
                                writer.Key(it->key().c_str());
                                writer.Int(std::stoi(it->value()));
                            }
                            writer.EndObject();
                        }

                        // end http
                        writer.EndObject();
                    }

                    writer.EndObject();

                    std::string json_string = s.GetString();

                    std::vector<std::string> child_json_vector;

                    child_span_[0].setBinaryAnnotations(binary_annotations_);

                    for (auto it = child_span_.begin(); it != child_span_.end(); it++) {
                        child_json_vector.push_back(it->toJson());
                    }
                    Util::addArrayToJson(json_string, child_json_vector, XRayJsonFieldNames::get().CHILD_SPAN.c_str());

                    std::string json_doc = header_json_string + "\n" + json_string;

                    std::cout << json_doc << std::endl;
                    return json_doc;
                }

                void Span::finish() {
                    if (auto t = tracer()) {
                        t->reportSpan(std::move(*this));
                    }
                }

                void Span::setTag(const std::string& name, const std::string& value) {
                    if (name.size() > 0 && value.size() > 0) {
                        addBinaryAnnotation(BinaryAnnotation(name, value));
                    }
                }
            }
        }
    }
}
