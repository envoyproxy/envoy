#pragma once
#include <string>
#include "cpp_sdk_inf.h"
#include "source/common/common/logger.h"

// Function prototype for the callback (C-style function pointer)
extern "C" {
    typedef size_t (*CallbackFunction)(
        void* filter_envoy_ptr,
        char* key,
        size_t key_length,
        envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr,
        size_t* result_buffer_length_ptr,
        size_t index
    );

    typedef bool (*CallbackFunction2)(
        void* filter_envoy_ptr,
        envoy_dynamic_module_type_http_header* result_headers
    );

    typedef bool (*CallbackFunction3)(
        void* filter_envoy_ptr,
        envoy_dynamic_module_type_envoy_buffer* result_buffer_vector
    );

    typedef bool (*CallbackVectorSz)(
        void* filter_envoy_ptr,
        size_t* size
    );

    typedef size_t (*CallbackCount)(
        const envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr
    );
}

struct EnvoyFilterConfig 
{
    EnvoyFilterConfig() = default;
    EnvoyFilterConfig(const void* raw, std::string name)
        : m_raw(raw), m_name(std::move(name)) {}

    const void* m_raw; // Pointer to the raw Envoy filter config
    std::string m_name;
};

class EnvoyHttpFilterImpl : public EnvoyHttpFilter
{
public:
    EnvoyHttpFilterImpl(envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr,
                        envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr):
                        m_dynamic_module_filter_ptr(filter_envoy_ptr),
                        m_filter_config(filter_config_ptr) {}
                        
    ~EnvoyHttpFilterImpl() override = default;

    std::optional<std::string> get_request_header_value(std::string_view key)        const override;
    std::vector<std::string>   get_request_header_values(std::string_view key)       const override;
    std::unordered_map<std::string_view, std::string_view> get_request_headers()     const override;
    bool set_request_header(std::string_view key, std::string_view val)              const override;
    bool remove_request_header(std::string_view key)                                 const override;
    std::optional<std::string> get_request_trailer_value(std::string_view key)       const override;
    std::vector<std::string>   get_request_trailer_values(std::string_view key)      const override;
    std::unordered_map<std::string_view, std::string_view> get_request_trailers()    const override;
    bool set_request_trailer(std::string_view key, std::string_view val)             const override;
    bool remove_request_trailer(std::string_view key)                                const override;
    std::optional<std::string> get_response_header_value(std::string_view key)       const override;
    std::vector<std::string> get_response_header_values(std::string_view key)        const override;
    std::unordered_map<std::string_view, std::string_view> get_response_headers()    const override;
    bool set_response_header(std::string_view key, std::string_view val)             const override;
    bool remove_response_header(std::string_view key)                                const override;
    std::optional<std::string> get_response_trailer_value(std::string_view key)      const override;
    std::vector<std::string> get_response_trailer_values(std::string_view key)       const override;
    std::unordered_map<std::string_view, std::string_view> get_response_trailers()   const override;
    bool set_response_trailer(std::string_view key, std::string_view val)            const override;
    bool remove_response_trailer(std::string_view key)                               const override;

    void send_response(uint32_t status_code, const std::vector<std::string_view>& headers, std::optional<std::string_view> body) const override;
    std::optional<double> get_dynamic_metadata_number(std::string_view nspace, std::string_view key) const override;
    bool set_dynamic_metadata_number(std::string_view nspace, std::string_view key, double value)    const override;
    std::optional<std::string_view> get_dynamic_metadata_string(std::string_view nspace, std::string_view key) const override;
    bool set_dynamic_metadata_string(std::string_view nspace, std::string_view key, std::string_view value)    const override;

    std::optional<std::string_view> get_filter_state_bytes(std::string key)  const override;
    bool set_filter_state_bytes(std::string key, std::string value)          const override;
    std::vector<std::string> get_request_body()                              const override;
    bool drain_request_body(size_t number_of_bytes)                          const override;
    bool append_request_body(uint8_t data)                                   const override;
    std::vector<std::string> get_response_body()                             const override;
    bool drain_response_body(size_t number_of_bytes)                         const override;
    bool append_response_body(uint8_t data)                                  const override;
    void clear_route_cache()                                                 const override;

    std::optional<std::string_view> get_attribute_string(envoy_dynamic_module_type_attribute_id attr_id) const override;
    std::optional<int64_t> get_attribute_int(envoy_dynamic_module_type_attribute_id attr_id) const override;

    envoy_dynamic_module_type_http_filter_config_module_ptr get_filter_config() const {
        return m_filter_config;
    }

    envoy_dynamic_module_type_http_filter_envoy_ptr get_module_ptr() const {
        return m_dynamic_module_filter_ptr;
    }
    
private:
    //pointer to Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilter
    void* m_dynamic_module_filter_ptr = nullptr; 
    const void* m_filter_config = nullptr;

    std::optional<std::string>      get_header_value_impl(std::string_view key, CallbackFunction callback) const;
    std::vector<std::string>        get_header_values_impl(std::string_view key, CallbackFunction callback) const;
    std::unordered_map<std::string_view, std::string_view>  get_headers_impl(CallbackCount count_cb, CallbackFunction2 callback) const;
    std::vector<std::string>        get_body_impl(CallbackVectorSz cbSz, CallbackFunction3 cb) const;
};
