#pragma once
#include <string_view>
#include <vector>
#include <unordered_map>
#include <optional>
#include <stdint.h>
#include "source/extensions/dynamic_modules/abi.h"

class EnvoyHttpFilter 
{
public:
    EnvoyHttpFilter() = default;
    virtual ~EnvoyHttpFilter() {}

    // Get the value of the request header with the given key.
    // If the header is not found, this returns `{}`.
    virtual std::optional<std::string> get_request_header_value(std::string_view key) const = 0;

    // Get the values of the request header with the given key.
    // If the header is not found, this returns an empty vector.
    virtual std::vector<std::string> get_request_header_values(std::string_view key) const = 0;

    // Get all request headers.
    // Returns a list of key-value pairs of the request headers.
    // If there are no headers or headers are not available, this returns an empty map.
    virtual std::unordered_map<std::string_view, std::string_view> get_request_headers() const = 0;

    // Set the request header with the given key and value.
    // This will overwrite the existing value if the header is already present.
    // In case of multiple values for the same key, this will remove all the existing values and set the new value.
    // Returns true if the header is set successfully.
    virtual bool set_request_header(std::string_view key, std::string_view val) const = 0;

    // Remove the request header with the given key.
    // Returns true if the header is removed successfully.
    virtual bool remove_request_header(std::string_view key) const = 0;

    // Get the value of the request trailer with the given key.
    // If the header is not found, this returns `{}`.
    virtual std::optional<std::string> get_request_trailer_value(std::string_view key) const = 0;

    // Get the values of the request trailer with the given key.
    // If the header is not found, this returns an empty vector.
    virtual std::vector<std::string>   get_request_trailer_values(std::string_view key) const = 0;

    // Get all request trailers.
    // Returns a list of key-value pairs of the request trailers.
    // If there are no trailers or trailers are not available, this returns an empty map.
    virtual std::unordered_map<std::string_view, std::string_view> get_request_trailers() const = 0;

    // Set the request trailer with the given key and value.
    // This will overwrite the existing value if the trailer is already present.
    // In case of multiple values for the same key, this will remove all the existing values and set the new value.
    // Returns true if the trailer is set successfully.
    virtual bool set_request_trailer(std::string_view key, std::string_view val) const = 0;

    // Remove the request trailer with the given key.
    // Returns true if the trailer is removed successfully.
    virtual bool remove_request_trailer(std::string_view key) const = 0;

    // Get the value of the response header with the given key.
    // If the header is not found, this returns `{}`.
    // To handle multiple values for the same key, use [`EnvoyHttpFilter::get_response_header_values`] variant.
    virtual std::optional<std::string> get_response_header_value(std::string_view key) const = 0;

    // Get the values of the response header with the given key.
    // If the header is not found, this returns an empty vector.
    virtual std::vector<std::string> get_response_header_values(std::string_view key) const = 0;

    // Get all response headers.
    // Returns a list of key-value pairs of the response headers.
    // If there are no headers or headers are not available, this returns an empty list.
    virtual std::unordered_map<std::string_view, std::string_view> get_response_headers() const = 0;

    // Set the response header with the given key and value.
    // This will overwrite the existing value if the header is already present.
    // In case of multiple values for the same key, this will remove all the existing values and set the new value.
    // Returns true if the header is set successfully.
    virtual bool set_response_header(std::string_view key, std::string_view val) const = 0;

    // Remove the response header with the given key.
    // Returns true if the header is removed successfully.
    virtual bool remove_response_header(std::string_view key) const = 0;

    // Get the value of the response trailer with the given key.
    // If the trailer is not found, this returns `{}`.
    // To handle multiple values for the same key, use [`EnvoyHttpFilter::get_response_trailer_values`] variant.
    virtual std::optional<std::string> get_response_trailer_value(std::string_view key) const = 0;

    // Get the values of the response trailer with the given key.
    // If the trailer is not found, this returns an empty vector.
    virtual std::vector<std::string> get_response_trailer_values(std::string_view key) const = 0;

    // Get all response trailers.
    // Returns a list of key-value pairs of the response trailers.
    // If there are no trailers or trailers are not available, this returns an empty list.
    virtual std::unordered_map<std::string_view, std::string_view> get_response_trailers() const = 0;

    // Set the response trailer with the given key and value.
    // This will overwrite the existing value if the trailer is already present.
    // In case of multiple values for the same key, this will remove all the existing values and set the new value.
    // Returns true if the operation is successful.
    virtual bool set_response_trailer(std::string_view key, std::string_view val) const = 0;

    // Remove the response trailer with the given key.
    // Returns true if the trailer is removed successfully.
    virtual bool remove_response_trailer(std::string_view key) const = 0;

    // Send a response to the downstream with the given status code, headers, and body.
    // The headers are passed as a list of key-value pairs.
    virtual void send_response(uint32_t status_code, const std::vector<std::string_view>& headers, std::optional<std::string_view> body) const = 0;

    // Get the number-typed dynamic metadata value with the given key.
    // If the metadata is not found or is the wrong type, this returns `{}`.
    virtual std::optional<double> get_dynamic_metadata_number(std::string_view nspace, std::string_view key) const = 0;

    // Set the number-typed dynamic metadata value with the given key.
    // If the namespace is not found, this will create a new namespace.
    // Returns true if the operation is successful.
    virtual bool set_dynamic_metadata_number(std::string_view nspace, std::string_view key, double value) const = 0;

    // Get the string-typed dynamic metadata value with the given key.
    // If the metadata is not found or is the wrong type, this returns `{}`.
    virtual std::optional<std::string_view> get_dynamic_metadata_string(std::string_view nspace, std::string_view key) const = 0;

    // Set the string-typed dynamic metadata value with the given key.
    // If the namespace is not found, this will create a new namespace.
    // Returns true if the operation is successful.
    virtual bool set_dynamic_metadata_string(std::string_view nspace, std::string_view key, std::string_view value) const = 0;

    // Get the bytes-typed filter state value with the given key.
    // If the filter state is not found or is the wrong type, this returns `None`.
    virtual std::optional<std::string_view> get_filter_state_bytes(std::string key) const = 0;

    // Set the bytes-typed filter state value with the given key.
    // If the filter state is not found, this will create a new filter state.
    // Returns true if the operation is successful.
    virtual bool set_filter_state_bytes(std::string key, std::string value) const = 0;

    // Get the currently buffered request body. 
    // This returns None if the request body is not available.
    virtual std::vector<std::string> get_request_body() const = 0;

    // Drain the given number of bytes from the front of the request body.
    // Returns false if the request body is not available.
    // Note that after changing the request body, it is caller's responsibility to modify the content-length header if necessary.
    virtual bool drain_request_body(size_t number_of_bytes) const = 0;

    // Append the given data to the end of request body.
    // Returns false if the request body is not available.
    // Note that after changing the request body, it is caller's responsibility to modify the content-length header if necessary.
    virtual bool append_request_body(uint8_t data) const = 0;

    // Get the currently buffered response body. 
    // Returns {} if the response body is not available.
    virtual std::vector<std::string> get_response_body() const = 0;

    // Drain the given number of bytes from the front of the response body.
    // Returns false if the response body is not available.
    // Note that after changing the response body, it is caller's responsibility to modify the content-length header if necessary.
    virtual bool drain_response_body(size_t number_of_bytes) const = 0;

    // Append the given data to the end of the response body.
    // Returns false if the response body is not available.
    // Note that after changing the response body, it is caller's responsibility to modify the content-length header if necessary.
    virtual bool append_response_body(uint8_t data) const = 0;
    
    // Clear the route cache calculated during a previous phase of the filter chain.
    // This is useful when the filter wants to force a re-evaluation of the route selection after
    // modifying the request headers, etc that affect the routing decision.
    virtual void clear_route_cache() const = 0;

    // Get the value of the attribute with the given ID as a string.
    // If the attribute is not found, not supported or is the wrong type, this returns `{}`.
    virtual std::optional<std::string_view> get_attribute_string(envoy_dynamic_module_type_attribute_id attr_id) const = 0;

    // Get the value of the attribute with the given ID as an integer.
    // If the attribute is not found, not supported or is the wrong type, this returns `{}`.
    virtual std::optional<int64_t> get_attribute_int(envoy_dynamic_module_type_attribute_id attr_id) const = 0;
};


class HttpFilterInterface
{
public:
    virtual ~HttpFilterInterface() {};
    
    //returns RequestHeadersStatus 
    virtual int OnRequestHeaders(const EnvoyHttpFilter* e, bool endOfStream) = 0;

    //returns RequestBodyStatus 
    virtual int OnRequestBody(const EnvoyHttpFilter* e, bool endOfStream) = 0;

    // ResponseHeaders is called when the response headers are received.
	virtual int OnResponseHeaders(const EnvoyHttpFilter* e, bool endOfStream) = 0;

	// ResponseBody is called when the response body is received.
	virtual int OnResponseBody(const EnvoyHttpFilter* e, bool endOfStream) = 0;

    virtual int OnRequestTrailers(const EnvoyHttpFilter* e) = 0;

    virtual int OnResponseTrailers(const EnvoyHttpFilter* e) = 0;

    virtual void OnStreamComplete(const EnvoyHttpFilter* e) = 0;

    //Destroy is called when the stream is destroyed.
	virtual void Destroy() = 0;
};
