#pragma once

#include "library/common/types/c_types.h"

class ManagedEnvoyHeaders {
    public:
        ManagedEnvoyHeaders(envoy_headers headers): headers_(headers) {};
        ~ManagedEnvoyHeaders() { release_envoy_headers(headers_); }
        const envoy_headers get() const { return headers_; }
    private:
        envoy_headers headers_;
        ManagedEnvoyHeaders(const ManagedEnvoyHeaders&);
        ManagedEnvoyHeaders& operator=(const ManagedEnvoyHeaders&);
};