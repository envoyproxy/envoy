Added helpers to the dynamic module SDKs that check a header name or value before a module passes
it to Envoy: ``is_valid_header_name`` and ``is_valid_header_value`` in the Rust SDK,
``IsValidHeaderName`` and ``IsValidHeaderValue`` in the Go SDK, and ``Utility::isValidHeaderName``
and ``Utility::isValidHeaderValue`` in the C++ SDK. Envoy does not check the header names and values
that modules pass unless a callback's documentation says it does, so a module that builds a header
from request data should check it first. The ABI header now documents this.
