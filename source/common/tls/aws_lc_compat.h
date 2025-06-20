#pragma once

// Aws-lc can be utilized as an alternative to boringssl
// This file provides API translation from boringssl to aws-lc when Envoy is compiled with aws-lc
// As of now, aws-lc is only compiled with Envoy for the ``ppc64le`` platform
// More information about aws-lc can be found here: https://github.com/aws/aws-lc
// This file should be included wherever the following identifiers are invoked by Envoy

namespace Envoy {

#ifdef OPENSSL_IS_AWSLC
#define sk_X509_NAME_find sk_X509_NAME_find_awslc
#endif

} // namespace Envoy
