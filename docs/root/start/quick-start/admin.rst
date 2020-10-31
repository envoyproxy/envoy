.. _start_quick_start_admin:

Envoy admin interface
=====================

The optional admin interface provided by Envoy allows you to dump configuration and statistics, change the
behaviour of the server, and tap traffic according to specific filter rules.

The admin interface can be configured for static and dynamic setups.

``admin``
---------

The :ref:`admin message <envoy_v3_api_msg_config.bootstrap.v3.Admin>` is required to enable and configure
the administration server.

The ``address`` key specifies the listening :ref:`address <envoy_v3_api_file_envoy/config/core/v3/address.proto>`
which in the demo configuration is ``0.0.0.0:9901``.

.. code-block:: yaml

   admin:
     access_log_path: /dev/null
     address:
       socket_address:
         address: 0.0.0.0
	 port_value: 19000

.. warning::

   You may wish to restrict the network address the admin server listens to in your own deployment.


Admin endpoints: ``config_dump``
--------------------------------


Admin endpoints: ``stats``
--------------------------
