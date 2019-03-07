#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/network/address.h"

#include "common/common/hex.h"
#include "absl/types/optional.h"
#include "common/common/logger.h"

#include "extensions/tracers/xray/tracer_interface.h"

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {

                /**
                 * Base class to be inherited by all classes that represent XRay-related concepts, namely:
                 * annotation, and span.
                 */
                class XRayBase {
                public:
                    /**
                     * Destructor.
                     */
                    virtual ~XRayBase() {}

                    /**
                     * All classes defining XRay abstractions need to implement this method to convert
                     * the corresponding abstraction to a XRay-compliant JSON.
                     */
                    virtual const std::string toJson() PURE;
                };

                /**
                 * Represents an Annotation. This class stores "string" type key-value pair.
                 */
                class Annotation : public XRayBase {
                public:
                    /**
                     * Copy constructor.
                     */
                    Annotation(const Annotation&);

                    /**
                     * Assignment operator.
                     */
                    Annotation& operator=(const Annotation&);

                    /**
                     * Default constructor. Creates an empty binary annotation.
                     */
                    Annotation() : key_(), value_() {}

                    /**
                     * Constructor that creates an annotation based on the given parameters.
                     *
                     * @param key The key name of the annotation.
                     * @param value The value associated with the key.
                     */
                    Annotation(const std::string& key, const std::string& value) : key_(key), value_(value) {}

                    /**
                     * @return the key attribute.
                     */
                    const std::string& key() const { return key_; }

                    /**
                     * Sets the key attribute.
                     */
                    void setKey(const std::string& key) { key_ = key; }

                    /**
                     * @return the value attribute.
                     */
                    const std::string& value() const { return value_; }

                    /**
                     * Sets the value attribute.
                     */
                    void setValue(const std::string& value) { value_ = value; }

                    /**
                     * Serializes the binary annotation as JSON representation as a string.
                     *
                     * @return a stringified JSON.
                     */
                    const std::string toJson() override;

                private:
                    std::string key_;
                    std::string value_;
                };

                /**
                 * Represents a XRay child span.
                 */
                class ChildSpan : public XRayBase {
                public:
                    /**
                     * Copy constructor.
                     */
                    ChildSpan(const ChildSpan&);

                    /**
                     * Default constructor. Creates an empty child span.
                     */
                    ChildSpan() : name_(), id_(0), start_time_(0) {}

                    /**
                     * Sets the child span's name attribute.
                     */
                    void setName(const std::string& val) { name_ = val; }

                    /**
                     * Sets the child span's id attribute.
                     */
                    void setId(const uint64_t val) { id_ = val; }

                    /**
                    * Sets the child span's start time attribute.
                    */
                    void setStartTime(const double time) { start_time_ = time; }

                    /**
                     * Sets the child span's annotation attribute at once.
                     */
                    void setAnnotations(const std::vector<Annotation>& val) { annotations_ = val; }

                    /**
                     * Adds the child span's annotation attribute.
                     */
                    void addAnnotation(const Annotation& bann) { annotations_.push_back(bann); }

                    void addAnnotation(const Annotation&& bann) { annotations_.push_back(bann); }

                    /**
                     * @return the child span's annotations attribute.
                     */
                    const std::vector<Annotation>& annotations() const { return annotations_; }

                    /**
                     * @return the child span's id attribute.
                     */
                    uint64_t id() const { return id_; }

                    /**
                     * @return the child span's id hex string represent.
                     */
                    const std::string idAsHexString() const { return Hex::uint64ToHex(id_); }

                    /**
                     * @return the child span's name attribute.
                     */
                    const std::string& name() const { return name_; }

                    /**
                     * @return the child span's start time attribute.
                     */
                    double startTime() const { return start_time_; }

                    /**
                     * Serializes the child span as a JSON representation as a string.
                     *
                     * @return a stringified JSON.
                     */
                    const std::string toJson() override;

                private:
                    std::string name_;
                    uint64_t id_;
                    std::vector<Annotation> annotations_;
                    double start_time_;
                };

                typedef std::unique_ptr<Span> SpanPtr;

                /**
                 * Represents a XRay span.
                 */
                class Span : public XRayBase, Logger::Loggable<Logger::Id::tracing> {
                public:
                    /**
                     * Copy constructor.
                     */
                    Span(const Span &);

                    /**
                     * Default constructor. Creates an empty span.
                     */
                    Span() : trace_id_(), name_(), id_(0), sampled_(false), start_time_(0) {}

                    /**
                     * Sets the span's trace id attribute.
                     */
                    void setTraceId(const std::string& val) { trace_id_ = val; }

                    /**
                     * Sets the span's name attribute.
                     */
                    void setName(const std::string& val) { name_ = val; }

                    /**
                     * Sets the span's id.
                     */
                    void setId(const uint64_t val) { id_ = val; }

                    /**
                     * Sets the span's parent id.
                     */
                    void setParentId(const uint64_t val) { parent_id_ = val; }

                    /**
                     * @return Whether or not the parent_id attribute is set.
                     */
                    bool isSetParentId() const { return parent_id_.has_value(); }

                    /**
                     * Set the span's sampled flag.
                     */
                    void setSampled(bool val) { sampled_ = val; }

                    /**
                     * Sets the span's annotations all at once.
                     */
                    void setAnnotations(const std::vector<Annotation>& val) { annotations_ = val; }

                    /**
                     * Adds a annotation to the span (copy semantics).
                     */
                    void addAnnotation(const Annotation& bann) { annotations_.push_back(bann); }

                    /**
                     * Adds a annotation to the span (move semantics).
                     */
                    void addAnnotation(const Annotation&& bann) { annotations_.push_back(bann); }

                    /**
                     * @return the span's annotations attribute.
                     */
                    const std::vector<Annotation>& annotations() const { return annotations_; }

                    /**
                     * Sets the span's child spans all at once.
                     */
                    void setChildSpans(const std::vector<ChildSpan>& val) { child_span_ = val; }

                    /**
                     * Adds the span's child span.
                     */
                    void addChildSpan(const ChildSpan& child) {
                        child_span_.push_back(child);
                    }

                    /**
                     * Adds the span's child span.
                     */
                    void addChildSpan(const ChildSpan&& child) {
                        child_span_.push_back(child);
                    }

                    /**
                     * @return the span's child spans.
                     */
                    const std::vector<ChildSpan>& childSpans() const { return child_span_; }

                    /**
                     * Sets the span's operation name attribute.
                     */
                    void setOperationName(const std::string& operation) { operation_name_ = operation; }

                    /**
                     * Sets the span's start time attribute.
                     */
                    void setStartTime(const double time) { start_time_ = time; }

                    /**
                     * @return the span's id as an integer.
                     */
                    uint64_t id() const { return id_; }

                    /**
                     * @return the span's id as a hexadecimal string.
                     */
                    const std::string idAsHexString() const { return Hex::uint64ToHex(id_); }

                    /**
                    * @return the span's parent id as an integer.
                    */
                    uint64_t parentId() const { return parent_id_.value(); }

                    /**
                     * @return the span's parent id as a hexadecimal string.
                     */
                    const std::string parentIdAsHexString() const {
                        return parent_id_ ? Hex::uint64ToHex(parent_id_.value()) : EMPTY_HEX_STRING_;
                    }

                    /**
                     * @return the span's name.
                     */
                    const std::string& name() const { return name_; }

                    /**
                    * @return the span's operation name.
                    */
                    const std::string& operationName() const { return operation_name_; }

                    /**
                     * @return whether or not the sampled attribute is set
                     */
                    bool sampled() const { return sampled_; }

                    /**
                     * @return the span's trace id as a string.
                     */
                    const std::string traceId() const { return trace_id_; }

                    /**
                     * @return the span's start time.
                     */
                    double startTime() const { return start_time_; }

                    /**
                     * Serializes the span as a XRay-compliant JSON representation as a string.
                     *
                     * @return a stringified JSON.
                     */
                    const std::string toJson() override;

                    /**
                     * Associates a Tracer object with the span. The tracer's reportSpan() method is invoked
                     * by the span's finish() method so that the tracer can send span when it is finished.
                     *
                     * @param tracer Represents the Tracer object to be associated with the span.
                     */
                    void setTracer(TracerInterface* tracer) { tracer_ = tracer; }

                    /**
                     * @return the Tracer object associated with the span.
                     */
                    TracerInterface* tracer() const { return tracer_; }

                    /**
                     * Marks a successful end of the span. This method will: invoke the tracer's reportSpan() method if a tracer has been associated with the span.
                     */
                    void finish();

                    /**
                     * Adds an annotation to the span.
                     *
                     * @param name The annotation's key.
                     * @param value The annotation's value.
                     */
                    void setTag(const std::string& name, const std::string& value);

                private:
                    static const std::string EMPTY_HEX_STRING_;
                    static const std::string VERSION_;
                    static const std::string FORMAT_;

                    std::string trace_id_;
                    std::string name_;
                    uint64_t id_;
                    absl::optional<uint64_t> parent_id_;
                    bool sampled_;
                    std::vector<Annotation> annotations_;
                    double start_time_;
                    TracerInterface* tracer_;
                    std::vector<ChildSpan> child_span_;
                    std::string operation_name_;
                };
            } // namespace XRay
        } // namespace Tracers
    } // namespace Extensions
} // namespace Envoy
