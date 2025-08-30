#include "cpp_sdk_impl.h"
//#include "source/extensions/filters/http/dynamic_modules/filter.h"

/*
    this pointer is passed to callbacks, where it's typecast to DynamicModuleHttpFilter.
*/

//#define CAST_TO_MODULE \
 //   reinterpret_cast<Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilter*>(m_dynamic_module_filter_ptr)

#define CAST_TO_MODULE m_dynamic_module_filter_ptr

std::optional<std::string> EnvoyHttpFilterImpl::get_header_value_impl(std::string_view key, CallbackFunction callback) const 
{
    auto cast_ptr = CAST_TO_MODULE;
    if(!cast_ptr) {
        return std::nullopt;
    }

    envoy_dynamic_module_type_buffer_envoy_ptr resultPtr = nullptr;
    size_t resultSize  = 0;

    //callbacks are implemented in source/extensions/filters/http/dynamic_modules/abi_impl.cc
    int size = callback(cast_ptr, const_cast<char*>(key.data()), key.size(), &resultPtr, &resultSize, 0);
    if(size == 0 || !resultPtr) {
        return std::nullopt;
    }
    
    return std::string(static_cast<char*>(resultPtr), resultSize);
}

std::vector<std::string> EnvoyHttpFilterImpl::get_header_values_impl(std::string_view key, CallbackFunction callback) const
{
    auto cast_ptr = CAST_TO_MODULE;
    if(!cast_ptr) {
        return {};
    }

    envoy_dynamic_module_type_buffer_envoy_ptr resultPtr = nullptr;
    size_t resultSize  = 0;

    //callbacks are implemented in source/extensions/filters/http/dynamic_modules/abi_impl.cc
    size_t count = callback(cast_ptr, const_cast<char*>(key.data()), key.size(), &resultPtr, &resultSize, 0);

    if(!resultPtr || count == 0) {
        return {};
    }

    std::vector<std::string> ret(count);
    for (size_t i = 0; i < count; ++i) {
        ret[i] = std::string(static_cast<char*>(resultPtr) + i * resultSize, resultSize);
    }

    return ret;
}

std::unordered_map<std::string_view, std::string_view> EnvoyHttpFilterImpl::get_headers_impl(
                                                    CallbackCount count_cb, CallbackFunction2 callback) const
{
    auto cast_ptr = CAST_TO_MODULE;
    if(!cast_ptr) {
        return {};
    }

    //callbacks are implemented in source/extensions/filters/http/dynamic_modules/abi_impl.cc
    size_t count = count_cb(cast_ptr);
    if(count == 0) {
        return {};
    }
    
    std::vector<envoy_dynamic_module_type_http_header> results(count);
    bool ret = callback(cast_ptr, results.data());
    if(!ret) {
        return {};
    }

    std::unordered_map<std::string_view, std::string_view> headers;
    for (size_t i = 0; i < count; ++i) {
        headers.insert(
            {std::string_view(results[i].key_ptr,   results[i].key_length),
             std::string_view(results[i].value_ptr, results[i].value_length)}
        );
    }

    return headers; 
}

std::vector<std::string> EnvoyHttpFilterImpl::get_body_impl(CallbackVectorSz cbSz, CallbackFunction3 cb) const
{
    auto cast_ptr = CAST_TO_MODULE;
    if(!cast_ptr) {
        return {};
    }

    size_t size = 0;
    bool ok = cbSz(cast_ptr, &size);
    if(!ok || size == 0) {
        return {};  
    }

    envoy_dynamic_module_type_envoy_buffer* buffer = 
        static_cast<envoy_dynamic_module_type_envoy_buffer*>(alloca(size*sizeof(envoy_dynamic_module_type_envoy_buffer)));

    bool success = cb(cast_ptr, buffer);
    if(!success) {
        return {};  
    }

    std::vector<std::string> ret(size);
    for (size_t i = 0; i < size; ++i) {
        ret[i] = std::string(static_cast<char*>(buffer[i].ptr), buffer[i].length);
    }   

    return ret;
}

std::optional<std::string> EnvoyHttpFilterImpl::get_request_header_value(std::string_view key) const
{
  //  ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_request_header_value, {}", key);
    return get_header_value_impl(key, envoy_dynamic_module_callback_http_get_request_header);
}

std::vector<std::string> EnvoyHttpFilterImpl::get_request_header_values(std::string_view key) const
{
  // ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_request_header_values, {}", key);
    return get_header_values_impl(key, envoy_dynamic_module_callback_http_get_request_header);
}

std::unordered_map<std::string_view, std::string_view> EnvoyHttpFilterImpl::get_request_headers() const
{
   // ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_request_headers");
    return get_headers_impl(envoy_dynamic_module_callback_http_get_request_headers_count,
                                        envoy_dynamic_module_callback_http_get_request_headers);
}

bool EnvoyHttpFilterImpl::set_request_header(std::string_view key, std::string_view val) const
{
   // ENVOY_LOG(info, "EnvoyHttpFilterImpl::set_request_header, key {}, val {}", key, val);
    return envoy_dynamic_module_callback_http_set_request_header(CAST_TO_MODULE, const_cast<char*>(key.data()), key.size(),
                                                                 const_cast<char*>(val.data()), val.size());
}

bool EnvoyHttpFilterImpl::remove_request_header(std::string_view key) const
{
   // ENVOY_LOG(info, "EnvoyHttpFilterImpl::remove_request_header, key {}", key);
    return envoy_dynamic_module_callback_http_set_request_header(CAST_TO_MODULE, const_cast<char*>(key.data()), key.size(),
                                                                 nullptr, 0);
}

std::optional<std::string> EnvoyHttpFilterImpl::get_request_trailer_value(std::string_view key) const
{
  //  ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_request_trailer_value, key {}", key);
    return get_header_value_impl(key, envoy_dynamic_module_callback_http_get_request_trailer);
}

std::vector<std::string> EnvoyHttpFilterImpl::get_request_trailer_values(std::string_view key) const
{
   // ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_request_trailer_values, key {}", key);
    return get_header_values_impl(key, envoy_dynamic_module_callback_http_get_request_trailer);
}

std::unordered_map<std::string_view, std::string_view> EnvoyHttpFilterImpl::get_request_trailers() const
{
  //  ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_request_trailers");
    return get_headers_impl(envoy_dynamic_module_callback_http_get_request_trailers_count,
                                            envoy_dynamic_module_callback_http_get_request_trailers);
}

bool EnvoyHttpFilterImpl::set_request_trailer(std::string_view key, std::string_view val) const
{
   // ENVOY_LOG(info, "EnvoyHttpFilterImpl::set_request_trailer, key {}, val {}", key, val);
    return envoy_dynamic_module_callback_http_set_request_trailer(CAST_TO_MODULE, const_cast<char*>(key.data()), key.size(),
                                                                  const_cast<char*>(val.data()), val.size());
}

bool EnvoyHttpFilterImpl::remove_request_trailer(std::string_view key) const
{
   // ENVOY_LOG(info, "EnvoyHttpFilterImpl::remove_request_trailer, key {}", key);
    return envoy_dynamic_module_callback_http_set_request_trailer(CAST_TO_MODULE, const_cast<char*>(key.data()), key.size(),
                                                                  nullptr, 0);
}

std::optional<std::string> EnvoyHttpFilterImpl::get_response_header_value(std::string_view key) const
{
   // ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_response_header_value, key {}", key);
    return get_header_value_impl(key, envoy_dynamic_module_callback_http_get_response_header);
}

std::vector<std::string> EnvoyHttpFilterImpl::get_response_header_values(std::string_view key) const
{
   // ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_response_header_values, key {}", key);
    return get_header_values_impl(key, envoy_dynamic_module_callback_http_get_response_header);
}

std::unordered_map<std::string_view, std::string_view> EnvoyHttpFilterImpl::get_response_headers() const
{
  //  ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_response_headers");
    return get_headers_impl(envoy_dynamic_module_callback_http_get_response_headers_count,
                                        envoy_dynamic_module_callback_http_get_response_headers);
}

bool EnvoyHttpFilterImpl::set_response_header(std::string_view key, std::string_view val) const 
{
   // ENVOY_LOG(info, "EnvoyHttpFilterImpl::set_response_header, key {}, val {}", key, val);
    return envoy_dynamic_module_callback_http_set_response_header(CAST_TO_MODULE, const_cast<char*>(key.data()), key.size(),
                                                                  const_cast<char*>(val.data()), val.size());
}

bool EnvoyHttpFilterImpl::remove_response_header(std::string_view key) const
{
   // ENVOY_LOG(info, "EnvoyHttpFilterImpl::remove_response_header, key {}", key);
    return envoy_dynamic_module_callback_http_set_response_header(CAST_TO_MODULE, const_cast<char*>(key.data()), key.size(),
                                                                  nullptr, 0);
}

std::optional<std::string> EnvoyHttpFilterImpl::get_response_trailer_value(std::string_view key) const
{
   // ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_response_trailer_value, key {}", key);
    return get_header_value_impl(key, envoy_dynamic_module_callback_http_get_response_trailer);
}

std::vector<std::string> EnvoyHttpFilterImpl::get_response_trailer_values(std::string_view key) const 
{
   // ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_response_trailer_values, key {}", key);
    return get_header_values_impl(key, envoy_dynamic_module_callback_http_get_response_trailer);
}

std::unordered_map<std::string_view, std::string_view> EnvoyHttpFilterImpl::get_response_trailers() const 
{
   // ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_response_trailers");
    return get_headers_impl(envoy_dynamic_module_callback_http_get_response_trailers_count,
                                            envoy_dynamic_module_callback_http_get_response_trailers);
}

bool EnvoyHttpFilterImpl::set_response_trailer(std::string_view key, std::string_view val) const
{
    //ENVOY_LOG(info, "EnvoyHttpFilterImpl::set_response_trailer, key {}, val {}", key, val);
    return envoy_dynamic_module_callback_http_set_response_trailer(CAST_TO_MODULE, const_cast<char*>(key.data()), key.size(),
                                                                   const_cast<char*>(val.data()), val.size());
}

bool EnvoyHttpFilterImpl::remove_response_trailer(std::string_view key) const
{
    //ENVOY_LOG(info, "EnvoyHttpFilterImpl::remove_response_trailer, key {}", key);
    return envoy_dynamic_module_callback_http_set_response_trailer(CAST_TO_MODULE, const_cast<char*>(key.data()), key.size(),
                                                                   nullptr, 0);
}

void EnvoyHttpFilterImpl::send_response(uint32_t status_code, const std::vector<std::string_view>& headers, std::optional<std::string_view> body) const
{
    //ENVOY_LOG(info, "EnvoyHttpFilterImpl::send_response, status_code {}", status_code);
    envoy_dynamic_module_type_module_http_header* headers_vector = nullptr;
    size_t headers_vector_size = 0;

    if (!headers.empty()) {
        headers_vector_size = headers.size();
        headers_vector = static_cast<envoy_dynamic_module_type_module_http_header*>(
            malloc(headers_vector_size * sizeof(envoy_dynamic_module_type_module_http_header)));
        for (size_t i = 0; i < headers_vector_size; ++i) {
            headers_vector[i].key_ptr = const_cast<char*>(headers[i].data());
            headers_vector[i].key_length = headers[i].size();
            headers_vector[i].value_ptr = nullptr;
            headers_vector[i].value_length = 0;
        }
    }

    envoy_dynamic_module_callback_http_send_response(CAST_TO_MODULE, status_code, headers_vector, headers_vector_size,
                                                     body ? const_cast<char*>(body->data()) : nullptr,
                                                     body ? body->size() : 0);

    delete[] headers_vector; // Clean up allocated memory
}

std::optional<double> EnvoyHttpFilterImpl::get_dynamic_metadata_number(std::string_view nspace, std::string_view key) const
{
   // ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_dynamic_metadata_number, nspace {}, key {}", nspace, key);
    double result = 0.0;
    bool ok = envoy_dynamic_module_callback_http_get_dynamic_metadata_number(
        CAST_TO_MODULE, const_cast<char*>(nspace.data()), nspace.size(),
        const_cast<char*>(key.data()), key.size(), &result);
    
    if (!ok) {
        return std::nullopt;
    }
    
    return result;
}

bool EnvoyHttpFilterImpl::set_dynamic_metadata_number(std::string_view nspace, std::string_view key, double value) const
{
    //ENVOY_LOG(info, "EnvoyHttpFilterImpl::set_dynamic_metadata_number, nspace {}, key {}, value {}", nspace, key, value);
    return envoy_dynamic_module_callback_http_set_dynamic_metadata_number(
        CAST_TO_MODULE, const_cast<char*>(nspace.data()), nspace.size(),
        const_cast<char*>(key.data()), key.size(), value);
}

std::optional<std::string_view> EnvoyHttpFilterImpl::get_dynamic_metadata_string(std::string_view nspace, std::string_view key) const
{
    //ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_dynamic_metadata_string, nspace {}, key {}", nspace, key);
    envoy_dynamic_module_type_buffer_envoy_ptr resultPtr = nullptr;
    size_t resultSize = 0;

    bool ok = envoy_dynamic_module_callback_http_get_dynamic_metadata_string(
        CAST_TO_MODULE, const_cast<char*>(nspace.data()), nspace.size(),
        const_cast<char*>(key.data()), key.size(), &resultPtr, &resultSize);

    if (!ok || !resultPtr) {
        return std::nullopt;
    }

    return std::string(static_cast<char*>(resultPtr), resultSize);
}

bool EnvoyHttpFilterImpl::set_dynamic_metadata_string(std::string_view nspace, std::string_view key, std::string_view value) const 
{
    //ENVOY_LOG(info, "EnvoyHttpFilterImpl::set_dynamic_metadata_string, nspace {}, key {}, value {}", nspace, key, value);
    return envoy_dynamic_module_callback_http_set_dynamic_metadata_string(
        CAST_TO_MODULE, const_cast<char*>(nspace.data()), nspace.size(),
        const_cast<char*>(key.data()), key.size(),
        const_cast<char*>(value.data()), value.size());
}

std::optional<std::string_view> EnvoyHttpFilterImpl::get_filter_state_bytes(std::string key) const
{
    //ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_filter_state_bytes, key {}", key);
    envoy_dynamic_module_type_buffer_envoy_ptr resultPtr = nullptr;
    size_t resultSize = 0;
    
    bool ok = envoy_dynamic_module_callback_http_get_filter_state_bytes(
        CAST_TO_MODULE, key.data(), key.size(), &resultPtr, &resultSize);

    if (!ok || !resultPtr) {
        return std::nullopt;
    }

    return std::string(static_cast<char*>(resultPtr), resultSize);
}

bool EnvoyHttpFilterImpl::set_filter_state_bytes(std::string key, std::string value) const
{
    //ENVOY_LOG(info, "EnvoyHttpFilterImpl::set_filter_state_bytes, key {}, value {}", key, value);
    return envoy_dynamic_module_callback_http_set_filter_state_bytes(
        CAST_TO_MODULE, key.data(), key.size(), value.data(), value.size());
}

std::vector<std::string> EnvoyHttpFilterImpl::get_request_body() const
{
    //ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_request_body");
    return get_body_impl(envoy_dynamic_module_callback_http_get_request_body_vector_size,
                         envoy_dynamic_module_callback_http_get_request_body_vector);
}

bool EnvoyHttpFilterImpl::drain_request_body(size_t number_of_bytes) const
{
    //ENVOY_LOG(info, "EnvoyHttpFilterImpl::drain_request_body, number_of_bytes {}", number_of_bytes);
    return envoy_dynamic_module_callback_http_drain_request_body(CAST_TO_MODULE, number_of_bytes);
}

bool EnvoyHttpFilterImpl::append_request_body(uint8_t data) const
{
    //ENVOY_LOG(info, "EnvoyHttpFilterImpl::append_request_body, data {}", data);
    return envoy_dynamic_module_callback_http_append_request_body(CAST_TO_MODULE, reinterpret_cast<char*>(&data), sizeof(data));
}

std::vector<std::string> EnvoyHttpFilterImpl::get_response_body() const
{
    //ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_response_body");
    return get_body_impl(envoy_dynamic_module_callback_http_get_response_body_vector_size,
                         envoy_dynamic_module_callback_http_get_response_body_vector);
}

bool EnvoyHttpFilterImpl::drain_response_body(size_t number_of_bytes)  const
{
    //ENVOY_LOG(info, "EnvoyHttpFilterImpl::drain_response_body, number_of_bytes {}", number_of_bytes);
    return envoy_dynamic_module_callback_http_drain_response_body(CAST_TO_MODULE, number_of_bytes);
}

bool EnvoyHttpFilterImpl::append_response_body(uint8_t data) const
{
    //ENVOY_LOG(info, "EnvoyHttpFilterImpl::append_response_body, data {}", data);
    return envoy_dynamic_module_callback_http_append_response_body(CAST_TO_MODULE, reinterpret_cast<char*>(&data), sizeof(data));
}

void EnvoyHttpFilterImpl::clear_route_cache() const
{
    //ENVOY_LOG(info, "EnvoyHttpFilterImpl::clear_route_cache");
    envoy_dynamic_module_callback_http_clear_route_cache(CAST_TO_MODULE);
}

std::optional<std::string_view> EnvoyHttpFilterImpl::get_attribute_string(envoy_dynamic_module_type_attribute_id attr_id) const
{
    //ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_attribute_string, attr_id {}", static_cast<uint>(attr_id));
    envoy_dynamic_module_type_buffer_envoy_ptr resultPtr = nullptr;
    size_t resultSize = 0;

    bool ok = envoy_dynamic_module_callback_http_filter_get_attribute_string(
        CAST_TO_MODULE, attr_id, &resultPtr, &resultSize);

    if (!ok || !resultPtr) {
        return std::nullopt;
    }

    return std::string(static_cast<char*>(resultPtr), resultSize);
}

std::optional<int64_t> EnvoyHttpFilterImpl::get_attribute_int(envoy_dynamic_module_type_attribute_id attr_id) const 
{
    //ENVOY_LOG(info, "EnvoyHttpFilterImpl::get_attribute_int, attr_id {}", static_cast<uint>(attr_id));
    uint64_t result = 0;
    bool ok = envoy_dynamic_module_callback_http_filter_get_attribute_int(
        CAST_TO_MODULE, attr_id, &result);

    if (!ok) {
        return std::nullopt;
    }

    return result;
}