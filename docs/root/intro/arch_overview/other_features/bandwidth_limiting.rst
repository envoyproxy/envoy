.. _arch_overview_bandwidth_limit:

Bandwidth limiting
===================

Envoy supports local (non-distributed) bandwidth limiting of HTTP requests and response via the
:ref:`HTTP bandwidth limit filter <config_http_filters_bandwidth_limit>`. This can be activated
globally at the listener level or at a more specific level (e.g.: the virtual host or route level).

