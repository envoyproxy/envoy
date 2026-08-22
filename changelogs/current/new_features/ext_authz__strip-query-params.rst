Added ``strip_query_params`` to the external authorization filter to strip the
query string from the request path before sending the authorization request to the
authorization server. The behavior applies to both HTTP and gRPC authorization services.
See :ref:`ExtAuthz <envoy_v3_api_msg_extensions.filters.http.ext_authz.v3.ExtAuthz>`.
