#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {

                class XRayJsonFieldNameValues {
                public:
                    const std::string HEADER_FORMAT = "format";
                    const std::string HEADER_VERSION = "version";

                    const std::string SPAN_TRACE_ID = "trace_id";
                    const std::string SPAN_PARENT_ID = "parent_id";
                    const std::string SPAN_NAME = "name";
                    const std::string SPAN_ID = "id";
                    const std::string SPAN_START_TIME = "start_time";
                    const std::string SPAN_END_TIME = "end_time";
                    const std::string SPAN_TIMESTAMP = "timestamp";
                    const std::string SPAN_DURATION = "duration";
                    const std::string SPAN_HTTP_ANNOTATIONS = "http";
                    const std::string SPAN_REQUEST = "request";
                    const std::string SPAN_RESPONSE = "response";
                    const std::string SPAN_URL = "url";
                    const std::string SPAN_METHOD = "method";
                    const std::string SPAN_CLIENT_IP = "client_ip";
                    const std::string SPAN_USER_AGENT = "user_agent";
                    const std::string SPAN_STATUS = "status";
                    const std::string SPAN_NAMESPACE = "namespace";
                    const std::string SPAN_REMOTE = "remote";
                    const std::string SPAN_ORIGIN = "origin";
                    const std::string SPAN_ORIGIN_VALUE = "ServiceMesh::Envoy";

                    const std::string CHILD_SPAN = "subsegments";
                };

                typedef ConstSingleton<XRayJsonFieldNameValues> XRayJsonFieldNames;

            } // namespace XRay
        } // namespace Tracers
    } // namespace Extensions
} // namespace Envoy
