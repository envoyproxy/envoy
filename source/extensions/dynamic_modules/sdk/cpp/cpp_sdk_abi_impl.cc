#include "cpp_sdk_impl.h"
#include "source/extensions/dynamic_modules/abi_version.h"

/*
    Following are Events Hooks provided by abi.h, that must be implemented by sdk.
    Refer to source/extensions/dynamic_modules/abi.h for more details.

    A little note on types.
    envoy_dynamic_module_type_http_filter_envoy_ptr --> HttpFilterInterface, the interface coming in from shared object.
    envoy_dynamic_module_type_http_filter_module_ptr --> EnvoyHttpFilterImpl, the SDK.
    envoy_dynamic_module_type_http_filter_config_envoy_ptr --> EnvoyFilterConfig, the configuration of filter.
*/

#define CAST_TO_FILTER static_cast<HttpFilterInterface*>(filter_envoy_ptr)
#define CAST_TO_MODULE static_cast<const EnvoyHttpFilterImpl*>(filter_module_ptr)

const char* envoy_dynamic_module_on_program_init() 
{
    return Envoy::Extensions::DynamicModules::kAbiVersion;
}

//This is called by newDynamicModuleHttpFilterConfig, in source/extensions/filters/http/dynamic_modules/filter_config.cc
envoy_dynamic_module_type_http_filter_config_module_ptr
envoy_dynamic_module_on_http_filter_config_new(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    const char* name_ptr, size_t name_size, const char* config_ptr, size_t config_size)
{
    std::string name;
    if (name_ptr && name_size > 0) {
        name.assign(reinterpret_cast<const char*>(name_ptr), name_size);
    }

    std::vector<uint8_t> config;
    if (config_ptr && config_size > 0) {
        config.assign(config_ptr, config_ptr + config_size);
    }

    
    void* mem = malloc(sizeof(EnvoyFilterConfig));
    if(!mem) return nullptr;
    EnvoyFilterConfig* obj = new (mem) EnvoyFilterConfig(filter_config_envoy_ptr, name);
    return reinterpret_cast<envoy_dynamic_module_type_http_filter_config_module_ptr>(obj);

  //  return reinterpret_cast<envoy_dynamic_module_type_http_filter_config_module_ptr>(   
    //    new EnvoyFilterConfig(filter_config_envoy_ptr, name));
}

void envoy_dynamic_module_on_http_filter_config_destroy(
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr)
{
    const EnvoyFilterConfig* filter_config = reinterpret_cast<const EnvoyFilterConfig*>(filter_config_ptr);
    if (!filter_config) {
        return;
    }
    filter_config->~EnvoyFilterConfig();
    free(const_cast<void*>(filter_config_ptr));
   // delete static_cast<EnvoyFilterConfig*>(const_cast<void*>(filter_config_ptr));
}

envoy_dynamic_module_type_http_filter_module_ptr envoy_dynamic_module_on_http_filter_new(
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr,
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr)
{
    
    void* mem = malloc(sizeof(EnvoyHttpFilterImpl));
    if (!mem) return nullptr; 
    EnvoyHttpFilterImpl* obj = new (mem) EnvoyHttpFilterImpl(filter_config_ptr, filter_envoy_ptr);
    return reinterpret_cast<envoy_dynamic_module_type_http_filter_module_ptr>(obj);

  //  return reinterpret_cast<envoy_dynamic_module_type_http_filter_module_ptr>(
  //      new EnvoyHttpFilterImpl(filter_config_ptr, filter_envoy_ptr));
}

void envoy_dynamic_module_on_http_filter_destroy(
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr)
{
    const EnvoyHttpFilterImpl* filter_module = reinterpret_cast<const EnvoyHttpFilterImpl*>(filter_module_ptr);
    if (!filter_module) {
        return;
    }
    filter_module->~EnvoyHttpFilterImpl();
    free(const_cast<void*>(filter_module_ptr));
   // delete static_cast<EnvoyHttpFilterImpl*>(const_cast<void*>(filter_module_ptr));
}


envoy_dynamic_module_type_on_http_filter_request_headers_status
envoy_dynamic_module_on_http_filter_request_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, bool end_of_stream)
{
    auto filter = static_cast<HttpFilterInterface*>(filter_envoy_ptr);
    if(!filter) {
        return envoy_dynamic_module_type_on_http_filter_request_headers_status_StopIteration;
    }

    //std::cout<<"***filter_module_ptr "<<filter_module_ptr<<std::endl;
    const EnvoyHttpFilterImpl* filter_module = reinterpret_cast<const EnvoyHttpFilterImpl*>(filter_module_ptr);
    //std::cout<<"***casted pointer "<<(filter_module ? "not null" : "nullptr")<<std::endl;

    auto status = filter->OnRequestHeaders(filter_module, end_of_stream);
    return static_cast<envoy_dynamic_module_type_on_http_filter_request_headers_status>(status);

//    return envoy_dynamic_module_type_on_http_filter_request_headers_status_Continue;
}

envoy_dynamic_module_type_on_http_filter_request_body_status
envoy_dynamic_module_on_http_filter_request_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, bool end_of_stream)
{
    auto filter = static_cast<HttpFilterInterface*>(filter_envoy_ptr);
    if(!filter) {
        return envoy_dynamic_module_type_on_http_filter_request_body_status_StopIterationNoBuffer;
    }

    const EnvoyHttpFilterImpl* filter_module = reinterpret_cast<const EnvoyHttpFilterImpl*>(filter_module_ptr);
    auto status = filter->OnRequestBody(filter_module, end_of_stream);
    return static_cast<envoy_dynamic_module_type_on_http_filter_request_body_status>(status);
}

envoy_dynamic_module_type_on_http_filter_request_trailers_status
envoy_dynamic_module_on_http_filter_request_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr)
{
    auto filter = static_cast<HttpFilterInterface*>(filter_envoy_ptr);
    if(!filter) {
        return envoy_dynamic_module_type_on_http_filter_request_trailers_status_StopIteration;
    }

    const EnvoyHttpFilterImpl* filter_module = reinterpret_cast<const EnvoyHttpFilterImpl*>(filter_module_ptr);
    auto status = filter->OnRequestTrailers(filter_module);
    return static_cast<envoy_dynamic_module_type_on_http_filter_request_trailers_status>(status);
}

envoy_dynamic_module_type_on_http_filter_response_headers_status
envoy_dynamic_module_on_http_filter_response_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, bool end_of_stream)
{
    auto filter = static_cast<HttpFilterInterface*>(filter_envoy_ptr);
    if(!filter) {
        return envoy_dynamic_module_type_on_http_filter_response_headers_status_StopIteration;
    }

    const EnvoyHttpFilterImpl* filter_module = reinterpret_cast<const EnvoyHttpFilterImpl*>(filter_module_ptr);
    auto status = filter->OnResponseHeaders(filter_module, end_of_stream);
    return static_cast<envoy_dynamic_module_type_on_http_filter_response_headers_status>(status);
}

envoy_dynamic_module_type_on_http_filter_response_body_status
envoy_dynamic_module_on_http_filter_response_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, bool end_of_stream)
{
    auto filter = static_cast<HttpFilterInterface*>(filter_envoy_ptr);
    if(!filter) {
        return envoy_dynamic_module_type_on_http_filter_response_body_status_StopIterationNoBuffer;
    }

    const EnvoyHttpFilterImpl* filter_module = reinterpret_cast<const EnvoyHttpFilterImpl*>(filter_module_ptr);
    auto status = filter->OnResponseBody(filter_module, end_of_stream);
    return static_cast<envoy_dynamic_module_type_on_http_filter_response_body_status>(status);
}

envoy_dynamic_module_type_on_http_filter_response_trailers_status
envoy_dynamic_module_on_http_filter_response_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr)
{
    auto filter = static_cast<HttpFilterInterface*>(filter_envoy_ptr);
    if(!filter) {
        return envoy_dynamic_module_type_on_http_filter_response_trailers_status_StopIteration;
    }

    const EnvoyHttpFilterImpl* filter_module = reinterpret_cast<const EnvoyHttpFilterImpl*>(filter_module_ptr);
    auto status = filter->OnResponseTrailers(filter_module);
    return static_cast<envoy_dynamic_module_type_on_http_filter_response_trailers_status>(status);
}

void envoy_dynamic_module_on_http_filter_stream_complete(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr)
{
    auto filter = static_cast<HttpFilterInterface*>(filter_envoy_ptr);
    if(!filter) {
        return;
    }

    const EnvoyHttpFilterImpl* filter_module = reinterpret_cast<const EnvoyHttpFilterImpl*>(filter_module_ptr);
    filter->OnStreamComplete(filter_module);
}
