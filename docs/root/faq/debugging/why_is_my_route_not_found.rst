.. _why_is_my_route_not_found:

Why is my route not found?
==========================

Once you've drilled down into Envoy responses and discovered Envoy generating local responses with the message
"Sending local reply with details route_not_found" the next question is _why_?

Often you can look at your route configuration and the headers sent, and see what is missing.
One often overlooked problem is host:port matching. If your route configuration matches the domain
www.host.com but the client is sending requests to www.host.com:443, it will not match.

If this is the problem you are encountering you can solve it one of two ways. First by changing your
configuration to match host:port pairs, going from

.. code-block:: yaml

  domains:
    - "www.host.com"

to

.. code-block:: yaml

  domains:
    - "www.host.com"
    - "www.host.com:80"
    - "www.host.com:443"

The other is to strip ports entirely using :ref:`strip_any_host_port <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.strip_any_host_port>` or
:ref:`strip_matching_host_port <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.strip_matching_host_port>`. The diffent is :ref:`strip_matching_host_port <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.strip_matching_host_port>`
only strip port if it is equal to the listener's local port.

