.. _arch_overview_http_conn_man:

HTTP connection management
==========================

HTTP is such a critical component of modern service oriented architectures that Envoy implements a
large amount of HTTP specific functionality. Envoy has a built in network level filter called the
:ref:`HTTP connection manager <config_http_conn_man>`. This filter translates raw bytes into HTTP
level messages and events (e.g., headers received, body data received, trailers received, etc.). It
also handles functionality common to all HTTP connections and requests such as :ref:`access logging
<arch_overview_http_access_logs>`, :ref:`request ID generation and tracing <arch_overview_tracing>`,
:ref:`request/response header manipulation <config_http_conn_man_headers>`, :ref:`route table
<arch_overview_http_routing>` management, and :ref:`statistics <config_http_conn_man_stats>`.

HTTP connection manager :ref:`configuration <config_http_conn_man>`.
