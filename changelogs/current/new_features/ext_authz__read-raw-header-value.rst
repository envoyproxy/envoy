Added :ref:`read_raw_header_value
<envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.read_raw_header_value>` to the
HTTP ``ext_authz`` filter. When set to ``true``, the gRPC ``ext_authz`` client reads header values
from :ref:`HeaderValue.raw_value <envoy_v3_api_field_config.core.v3.HeaderValue.raw_value>` when
non-empty, falling back to :ref:`HeaderValue.value
<envoy_v3_api_field_config.core.v3.HeaderValue.value>` when ``raw_value`` is empty, allowing gRPC
authorization servers to return non-UTF-8 header values. Also updated shadow mode so
:ref:`ShadowDecision.response_headers
<envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ShadowDecision.response_headers>`
populates ``raw_value`` instead of ``value`` when a denied response header value contains
non-UTF-8 bytes.
