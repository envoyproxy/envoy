Added ``strip_query_params`` to the HTTP external authorization filter to strip the
query string from the request ``:path`` header before sending the authorization request
to the HTTP authorization server. See :ref:`HttpService
<envoy_v3_api_msg_extensions.filters.http.ext_authz.v3.HttpService>`.
