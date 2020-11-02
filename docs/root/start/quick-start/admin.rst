.. _start_quick_start_admin:

Envoy admin interface
=====================

The optional admin interface provided by Envoy allows you to dump configuration and statistics, change the
behaviour of the server, and tap traffic according to specific filter rules.

The admin interface can be configured for static and dynamic setups.

Enabling the :ref:`admin <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.admin>` interface with
dynamic configuration can be particularly useful as it allows you to use the
:ref:`config_dump <start_quick_start_admin_config_dump>` endpoint to see how Envoy is configured at
a particular point in time.

This guide provides configuration information, and some basic examples of using a couple of the admin
endpoints.

See the :ref:`admin docs <operations_admin_interface>` for information on all of the available endpoints.

.. _start_quick_start_admin_config:

``admin``
---------

The :ref:`admin message <envoy_v3_api_msg_config.bootstrap.v3.Admin>` is required to enable and configure
the administration server.

The ``address`` key specifies the listening :ref:`address <envoy_v3_api_file_envoy/config/core/v3/address.proto>`
which in the demo configuration is ``0.0.0.0:9901``.

You must set the ``access_log_path`` to specify where to send access logs.

In this example, the logs are simply discarded.

.. code-block:: yaml
   :emphasize-lines: 2, 5-6

   admin:
     access_log_path: /dev/null
     address:
       socket_address:
         address: 0.0.0.0
	 port_value: 9901

.. warning::

   You may wish to restrict the network address the admin server listens to in your own deployment.

.. _start_quick_start_admin_config_dump:

Admin endpoints: ``config_dump``
--------------------------------

The :ref:`config_dump <operations_admin_interface_config_dump>` endpoint dumps Envoy's configuration
in ``json`` format.

These examples make use of the `jq <https://stedolan.github.io/jq/>`_ tool to parse the output
from ``config_dump``.

The following command allows you to see the types of config available:

.. code-block:: console

   $ curl -s http://localhost:9901/config_dump | jq -r '.configs[] | .["@type"]'
   type.googleapis.com/envoy.admin.v3.BootstrapConfigDump
   type.googleapis.com/envoy.admin.v3.ClustersConfigDump
   type.googleapis.com/envoy.admin.v3.ListenersConfigDump
   type.googleapis.com/envoy.admin.v3.ScopedRoutesConfigDump
   type.googleapis.com/envoy.admin.v3.RoutesConfigDump
   type.googleapis.com/envoy.admin.v3.SecretsConfigDump

To dump the ``socket_address`` of the first ``dynamic_listener`` currently configured, you could:

.. code-block:: console

   $ curl -s http://localhost:19000/config_dump?resource=dynamic_listeners | jq '.configs[0].active_state.listener.address'
   {
     "socket_address": {
       "address": "0.0.0.0",
       "port_value": 10000
     }
   }

See the reference section for :ref:`config_dump <operations_admin_interface_config_dump>` for further information
on available parameters and responses.

.. _start_quick_start_admin_stats:

Admin endpoints: ``stats``
--------------------------
