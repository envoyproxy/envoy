.. _config_http_filters_bandwidth_share:

Bandwidth share
===============

* Bandwidth limiting :ref:`architecture overview <arch_overview_bandwidth_limit>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.bandwidth_share.v3.BandwidthShare>`

The HTTP Bandwidth share filter limits the size of data flow to the max bandwidth set in the ``limit_kbps``
when the request's route, virtual host or filter chain has a
:ref:`bandwidth share configuration <envoy_v3_api_msg_extensions.filters.http.bandwidth_share.v3.BandwidthShare>`.

If the bandwidth limit has been exhausted the filter stops further transfer until more bandwidth gets allocated
according to the ``fill_interval`` (default is 50 milliseconds). If the connection buffer fills up with accumulated
data then the source of data will have ``readDisable(true)`` set as described in the :repo:`flow control doc<source/docs/flow_control.md>`.

When actively being limited, the filter splits the available bandwidth between active tenants by weight, and
between parallel requests for a single tenant evenly. For example, with six active requests divided
among three tenants:

====== ====== =============== ================
Tenant Weight Share to tenant Share to request
====== ====== =============== ================
foo    1      20%             6.7%
bar    3      60%             30%
foo    1      20%             6.7%
foo    1      20%             6.7%
bar    3      60%             30%
baz    1      20%             20%
====== ====== =============== ================

.. note::
  The token bucket is shared across all workers, thus the limits are applied per Envoy process.

Example configuration
---------------------

Example filter configuration for a globally enabled bandwidth share but disabled for a specific route:

.. literalinclude:: _include/bandwidth-share-filter.yaml
    :language: yaml
    :lines: 11-53
    :emphasize-lines: 9-25
    :caption: :download:`bandwidth-share-filter.yaml <_include/bandwidth-share-filter.yaml>`

Statistics
----------

The HTTP bandwidth share filter outputs statistics in the ``<stat_prefix>.bandwidth_share.`` namespace.

All metrics are emitted with the following tags:

* ``bucket_id`` (may be empty-string, as configured in ``request_limit`` or ``response_limit``)
* ``tenant`` (if not explicitly configured for inclusion with ``include_stats_tag`` in ``tenant_config``, will be empty-string)
* ``direction`` (``request`` or ``response``)

The counter ``bytes`` is additionally emitted with the following tag:

* ``handling`` (``limited`` or ``not_limited`` - ``not_limited`` is used for bytes for which the limiter remained in "fast" mode as the configured limit had not been approached.)

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  bytes, Counter, Number of bytes for which the bandwidth share filter was consulted.
  streams_currently_limited, Gauge, Number of streams that are currently experiencing delays.
  bytes_pending, Gauge, Number of bytes that are currently buffered due to delays.
