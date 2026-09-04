.. _arch_overview_bandwidth_limit:

Bandwidth limiting
===================

Envoy supports local (non-distributed) bandwidth limiting of HTTP requests and responses via the
:ref:`HTTP bandwidth limit filter <config_http_filters_bandwidth_limit>`. This can be activated
globally at the listener level or at a more specific level (e.g.: the virtual host or route level).

Envoy supports local (non-distributed) bandwidth fair-sharing of HTTP requests and responses, via the
:ref:`HTTP bandwidth share filter <config_http_filters_bandwidth_share>`. This can be activated
globally at the listener level or at a more specific level (e.g.: the virtual host or route level).
Bandwidth fair sharing is distinct from bandwidth limiting in that one limit can be weighted split
between tenants when overloaded.
