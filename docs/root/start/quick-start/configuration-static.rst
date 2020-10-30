.. _start_quick_start_static:

Configuration: Static
=====================

To start Envoy with static configuration, you will need to specify :ref:`listeners <start_quick_start_static_listeners>`
and :ref:`clusters <start_quick_start_static_clusters>` as
:ref:`static_resources <start_quick_start_static_static_resources>`.

You can also add an :ref:`admin <start_quick_start_static_admin>` section if you wish to monitor Envoy
or retrieve stats.

The following sections walk through the static configuration provided in the
:download:`demo configuration file <_include/envoy-demo.yaml>` used as the default in the Envoy Docker container.

.. _start_quick_start_static_static_resources:

``static_resources``
--------------------

The :ref:`static_resources <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.static_resources>` contain
everything that is configured statically when Envoy starts, as opposed to dynamically at runtime.

.. literalinclude:: _include/envoy-demo.yaml
    :language: yaml
    :linenos:
    :lines: 1-3
    :emphasize-lines: 1

.. _start_quick_start_static_listeners:

``listeners``
-------------

The example configures a :ref:`listener <envoy_v3_api_file_envoy/config/listener/v3/listener.proto>`
on port ``10000``.

All paths are matched and routed to the ``service_envoyproxy_io``
:ref:`cluster <start_quick_start_static_clusters>`.

.. literalinclude:: _include/envoy-demo.yaml
    :language: yaml
    :linenos:
    :lines: 1-29
    :emphasize-lines: 3-27

.. _start_quick_start_static_clusters:

``clusters``
------------

The ``service_envoyproxy_io`` :ref:`cluster <envoy_v3_api_file_envoy/service/cluster/v3/cds.proto>`
proxies over ``TLS`` to https://www.envoyproxy.io.

.. literalinclude:: _include/envoy-demo.yaml
    :language: yaml
    :lineno-start: 27
    :lines: 27-50
    :emphasize-lines: 3-22

.. _start_quick_start_static_admin:

``admin``
---------

The :ref:`admin message <envoy_v3_api_msg_config.bootstrap.v3.Admin>` is required to enable and configure
the administration server.

The ``address`` key specifies the listening :ref:`address <envoy_v3_api_file_envoy/config/core/v3/address.proto>`
which in the demo configuration is ``0.0.0.0:9901``.

.. literalinclude:: _include/envoy-demo.yaml
    :language: yaml
    :lineno-start: 48
    :lines: 48-55
    :emphasize-lines: 3-8

.. warning::

   You may wish to restrict the network address the admin server listens to in your own deployment.
