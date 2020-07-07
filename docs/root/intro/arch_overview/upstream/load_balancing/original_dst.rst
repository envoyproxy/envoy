.. _arch_overview_load_balancing_types_original_destination_request_header:

Original destination host request header
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Envoy can also pick up the original destination from a HTTP header called
:ref:`x-envoy-original-dst-host <config_http_conn_man_headers_x-envoy-original-dst-host>`.
Please note that fully resolved IP address should be passed in this header. For example if a request has to be
routed to a host with IP address 10.195.16.237 at port 8888, the request header value should be set as
``10.195.16.237:8888``.

