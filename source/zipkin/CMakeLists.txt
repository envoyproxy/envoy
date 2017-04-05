add_library(zipkin OBJECT tracer.cc util.cc zipkin_core_constants.cc zipkin_core_types
                .cc zipkin_json_field_names.cc span_context.cc span_buffer.cc)

    include_directories(${ENVOY_RAPIDJSON_INCLUDE_DIR})

        cotire(zipkin)
