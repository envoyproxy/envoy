.. _config_http_filters_tcp_post:

TCP Post
===============

The TcpPost filter is used by the upstream proxy to coordinate with downstream proxy that proxies
TCP streams over HTTP/2 POST requests. 

Configuration
-------------

* This filter should be configured with the name *envoy.filters.http.tcp_post*.

It converts POST requests back to CONNECT to trigger the regular TCP decapping. Therefore, it's
typically used with another localhost listener that handles the regular TCP decapping for CONNECT
requests.
