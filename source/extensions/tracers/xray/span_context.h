#pragma once

#include <regex>

#include "extensions/tracers/xray/util.h"
#include "extensions/tracers/xray/xray_core_constants.h"
#include "extensions/tracers/xray/xray_core_types.h"

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {
                class SpanContext {
                public:
                    /**
                     * Default constructor. Creates an empty context.
                     */
                    SpanContext() : trace_id_(), id_(0), parent_id_(0), sampled_(false) {}

                    /**
                     * Constructor that creates a context object from the supplied trace, span and
                     * parent ids, and sampled flag.
                     *

                     * @param trace_id The low 64 bits of the trace id.
                     * @param id The span id.
                     * @param parent_id The parent id.
                     * @param sampled The sampled flag.
                     */
                    SpanContext(const std::string trace_id, const uint64_t id, const uint64_t parent_id, bool sampled)
                            : trace_id_(trace_id), id_(id), parent_id_(parent_id),
                              sampled_(sampled) {}

                    /**
                     * Constructor that creates a context object from the given XRay span object.
                     *
                     * @param span The XRay span used to initialize a SpanContext object.
                     */
                    SpanContext(const Span& span);

                    /**
                     * @return the span id as an integer
                     */
                    uint64_t id() const { return id_; }

                    /**
                     * @return the span's parent id as an integer.
                     */
                    uint64_t parent_id() const { return parent_id_; }

                    /**
                     * @return the low 64 bits of the trace id as an integer.
                     */
                    std::string trace_id() const { return trace_id_; }

                    /**
                     * @return the sampled flag.
                     */
                    bool sampled() const { return sampled_; }

                private:
                    const std::string trace_id_;
                    const uint64_t id_;
                    const uint64_t parent_id_;
                    const bool sampled_;
                };

            } // namespace XRay
        } // namespace Tracers
    } // namespace Extensions
} // namespace Envoy
