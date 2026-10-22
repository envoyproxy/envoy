.. _config_istio_hbone:

Istio HBONE CONNECT wait
=======================

The contrib Istio HBONE CONNECT wait extension delays an internal upstream
connection's readiness until its HTTP/2 CONNECT stream is accepted. A transient
failure reaches the HTTP router as a connection failure before the router starts
sending the application request. Routes can retry it using ``connect-failure``,
including for POST requests.

This experimental extension pairs the ``envoy.transport_sockets.istio_hbone`` upstream
transport with the terminal ``envoy.filters.network.istio_hbone`` network filter on an
:ref:`internal listener <config_internal_listener>`, plus the
``envoy.upstreams.istio_hbone`` application connection pool adapter. All three extensions
must be compiled into the binary and configured together. The filter reuses TCP proxy's
CONNECT implementation, HTTP/2 pooling, flow control and half-close handling. CONNECT
streams continue to share outer HTTP/2 connections according to the existing pool keys.

Configuration
-------------

Retain the internal listener bootstrap extension and the existing HBONE upstream
cluster. That cluster must use HTTP/2 and the appropriate upstream mutual TLS settings,
including peer identity validation and the ``h2`` ALPN protocol. Use the default TCP
proxy connection pool implementation. Configure the application service cluster's
transport socket and pool adapter as follows:

.. code-block:: yaml

   transport_socket:
     name: envoy.transport_sockets.istio_hbone
     typed_config:
       "@type": type.googleapis.com/envoy.extensions.filters.network.istio_hbone.v3alpha.UpstreamConfig
       passthrough_metadata:
       - name: envoy.filters.listener.original_dst
         kind: {host: {}}
   upstream_config:
     name: envoy.upstreams.istio_hbone
     typed_config:
       "@type": type.googleapis.com/envoy.extensions.filters.network.istio_hbone.v3alpha.ConnectionPoolConfig

Keep the service cluster's existing internal endpoints and metadata. Replace the TCP
proxy on the receiving internal listener with the paired filter, nesting its existing
TCP proxy settings under ``tcp_proxy``:

.. code-block:: yaml

   name: connect_originate
   internal_listener: {}
   filter_chains:
   - filters:
     - name: envoy.filters.network.istio_hbone
       typed_config:
         "@type": type.googleapis.com/envoy.extensions.filters.network.istio_hbone.v3alpha.Config
         tcp_proxy:
           stat_prefix: connect_originate
           cluster: connect_originate
           tunneling_config:
             hostname: "%DOWNSTREAM_LOCAL_ADDRESS%"

Keep any existing original destination listener filter and address restoration needed
to produce the CONNECT authority. A fixed ``hostname: "app:8080"`` can be used when
testing one destination. CONNECT establishment must be immediate: POST tunneling,
waiting for downstream application data, and the filter-state override that disables
tunneling are rejected.

Retry behavior
--------------

On the application HTTP route, a minimal retry policy is:

.. code-block:: yaml

   retry_policy:
     retry_on: connect-failure
     num_retries: 1
     retry_host_predicate:
     - name: envoy.retry_host_predicates.previous_hosts
       typed_config:
         "@type": type.googleapis.com/envoy.extensions.retry.host.previous_hosts.v3.PreviousHostsPredicate
     host_selection_retry_max_attempts: 3

The service cluster's ``connect_timeout`` now includes CONNECT establishment. Set it
to accommodate the outer connection and destination connection attempt, while bounding
how long a router request waits. Normal request replay buffer limits and retry budgets
still apply. TCP proxy's own connection attempts occur before the router sees a final
failure; selecting another application endpoint requires a router retry.

When CONNECT fails:

* CONNECT 500--599 remains eligible for connection-failure retries, within the
  configured attempt and time budgets.
* CONNECT 4xx, including 400, 401, 403 and 429, is permanent. An HTTP caller receives
  a local reply with that status. A raw TCP caller is closed without forwarding data.
* Other rejected non-5xx statuses are permanent and produce a local 502 for HTTP
  callers. A CONNECT 200 that prematurely ends the stream is an establishment
  failure and retains the existing transient-failure behavior.

Permanent rejections bypass both the terminal TCP proxy's retry path and the
application router's retry path, including explicit retriable-status-code policies.
They do not report an outlier failure for either the selected service endpoint or
the HBONE peer. Application-generated responses inside accepted CONNECT streams
retain normal HTTP retry and outlier semantics.

Access logs identify a rejection with ``UF`` and
``istio_hbone_connect_rejected:<status>`` in the response code details and upstream
transport failure reason. The selected service endpoint remains in upstream info.
Connection-attempt failure counters can still increase; this does not imply an
outlier health failure. The local response is not charged as an application response
in the service cluster's upstream HTTP status counters.

Double HBONE
------------

Gate application readiness at the inner CONNECT using the same application
transport and adapter. Pair a shim filter on the outer origination listener with a
shim transport on the inner HTTP/2 cluster. Set that transport's ``transport_socket``
field to the existing inner TLS transport, retaining all identity validation,
certificate, SNI and ALPN settings.
Inner TLS starts only after outer CONNECT acceptance. Its own handshake must still
complete before the inner HTTP/2 pool becomes ready.

Each internal socket pair owns separate readiness state; no request-specific state
is added to the shared outer HTTP/2 pool key.

Failure boundaries
------------------

The transport blocks application reads and writes until acceptance. Readiness is
released on a later dispatcher iteration so a failure observed before that callback
cancels acceptance. This does not guarantee detection of a reset that arrives later.
After readiness is released, the normal established-connection behavior applies;
``connect-failure`` does not authorize a retry after application data may have reached
the destination.

A graceful outer HTTP/2 GOAWAY stops new streams on that connection while accepted
CONNECT streams can continue. It does not invalidate those streams or prevent an
existing application connection from carrying another request. A CONNECT rejection
before readiness follows the retry behavior above, including when caused by peer draining.

The scope is HBONE origination. This extension does not add termination, PROXY
protocol, or application TLS wrapping of the service transport. Existing metadata
filters and HBONE mutual TLS configuration remain necessary where applicable.

* :ref:`Network filter API <envoy_v3_api_msg_extensions.filters.network.istio_hbone.v3alpha.Config>`
* :ref:`Transport API <envoy_v3_api_msg_extensions.filters.network.istio_hbone.v3alpha.UpstreamConfig>`
* :ref:`Pool API <envoy_v3_api_msg_extensions.filters.network.istio_hbone.v3alpha.ConnectionPoolConfig>`
