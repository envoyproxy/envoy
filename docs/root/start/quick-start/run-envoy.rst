.. _start_quick_start_run_envoy:


Run Envoy
=========

The following instructions walk through starting Envoy as a system daemon or using
the Envoy Docker image.

.. _start_quick_start_version:

Check your Envoy version
------------------------

Once you have :ref:`installed Envoy <install>`, you can check the version information as follows:

.. tabs::

   .. tab:: System

      .. code-block:: console

	 $ envoy --version

   .. tab:: Docker

      .. substitution-code-block:: console

	 $ docker run --rm \
	       envoyproxy/|envoy_docker_image| \
	           --version

.. _start_quick_start_help:

View the Envoy command line options
-----------------------------------

You can view the Envoy :ref:`command line options <operations_cli>` with the ``--help``
flag:

.. tabs::

   .. tab:: System

      .. code-block:: console

	 $ envoy --help

   .. tab:: Docker

      .. substitution-code-block:: console

	 $ docker run --rm \
	       envoyproxy/|envoy_docker_image| \
	           --help

.. _start_quick_start_config:

Run Envoy with the demo configuration
-------------------------------------

The ``-c`` or ``--config-path`` flag tells Envoy the path to its initial configuration.

.. tabs::

   .. tab:: System

      To start Envoy as a system daemon :download:`download the demo configuration <_include/envoy-demo.yaml>`, and start
      as follows:

      .. code-block:: console

	 $ envoy -c envoy-demo.yaml

   .. tab:: Docker

      You can start the Envoy Docker image without specifying a configuration file, and
      it will use the demo config by default.

      .. substitution-code-block:: console

	 $ docker run --rm -d \
               -p 9901:9901 \
	       -p 10000:10000 \
	       envoyproxy/|envoy_docker_image|

      To specify a custom configuration you can mount the config into the container, and specify the path with ``-c``.

      Assuming you have a custom configuration in the current directory named ``envoy-custom.yaml``:

      .. substitution-code-block:: console

	 $ docker run --rm -d \
	       -v $(pwd)/envoy-custom.yaml:/envoy-custom.yaml \
	       -p 9901:9901 \
	       -p 10000:10000 \
	       envoyproxy/|envoy_docker_image| \
	           -c /envoy-custom.yaml

Check Envoy is proxying on http://localhost:10000

.. code-block:: console

   $ curl -v localhost:10000

The Envoy admin endpoint should also be available at http://localhost:9901

.. code-block:: console

   $ curl -v localhost:9901

.. _start_quick_start_override:

Override the default configuration by merging a config file
-----------------------------------------------------------

You can provide a configuration override file using ``--config-yaml`` which will merge with the main
configuration.

Save the following snippet to ``envoy-override.yaml``:

.. code-block:: yaml

   listeners:
     - name: listener_0
       address:
         socket_address:
           port_value: 20000

Next, start the Envoy server using the override configuration.

.. tabs::

   .. tab:: System

      .. code-block:: console

	 $ envoy -c envoy-demo.yaml --config-yaml envoy-override.yaml

   .. tab:: Docker

      .. substitution-code-block:: console

	 $ docker run --rm -d \
	       -v $(pwd)/envoy-override.yaml:/envoy-override.yaml \
	       -p 20000:20000 \
	       envoyproxy/|envoy_docker_image| \
	           --config-yaml /envoy-override.yaml

Envoy should now be proxying on http://localhost:20000

.. code-block:: console

   $ curl -v localhost:20000

The Envoy admin endpoint should also be available at http://localhost:9901

.. code-block:: console

   $ curl -v localhost:9901

Validating your Envoy configuration
-----------------------------------

You can start Envoy in :option:`validation mode <--mode>`.

This allows you to check that Envoy is able to start with your configuration, without actually starting
or restarting the service, or making any network connections.

If the configuration is valid the process will print ``OK`` and exit with a return code of ``0``.

For invalid configuration the process will print the errors and exit with ``1``.

.. tabs::

   .. tab:: System

      .. code-block:: console

	 $ envoy --mode validate -c my-envoy-config.yaml
	 [2020-11-08 12:36:06.543][11][info][main] [source/server/server.cc:583] runtime: layers:
	 - name: base
	   static_layer:
	     {}
	 - name: admin
	   admin_layer:
	     {}
	 [2020-11-08 12:36:06.543][11][info][config] [source/server/configuration_impl.cc:95] loading tracing configuration
	 [2020-11-08 12:36:06.543][11][info][config] [source/server/configuration_impl.cc:70] loading 0 static secret(s)
	 [2020-11-08 12:36:06.543][11][info][config] [source/server/configuration_impl.cc:76] loading 1 cluster(s)
	 [2020-11-08 12:36:06.546][11][info][config] [source/server/configuration_impl.cc:80] loading 1 listener(s)
	 [2020-11-08 12:36:06.549][11][info][config] [source/server/configuration_impl.cc:121] loading stats sink configuration
	 configuration 'my-envoy-config.yaml' OK

   .. tab:: Docker

      .. substitution-code-block:: console

	 $ docker run --rm \
	       -v $(pwd)/my-envoy-config.yaml:/my-envoy-config.yaml \
	       envoyproxy/|envoy_docker_image| \
	       --mode validate \
	       -c my-envoy-config.yaml
	 [2020-11-08 12:36:06.543][11][info][main] [source/server/server.cc:583] runtime: layers:
	 - name: base
	   static_layer:
	     {}
	 - name: admin
	   admin_layer:
	     {}
	 [2020-11-08 12:36:06.543][11][info][config] [source/server/configuration_impl.cc:95] loading tracing configuration
	 [2020-11-08 12:36:06.543][11][info][config] [source/server/configuration_impl.cc:70] loading 0 static secret(s)
	 [2020-11-08 12:36:06.543][11][info][config] [source/server/configuration_impl.cc:76] loading 1 cluster(s)
	 [2020-11-08 12:36:06.546][11][info][config] [source/server/configuration_impl.cc:80] loading 1 listener(s)
	 [2020-11-08 12:36:06.549][11][info][config] [source/server/configuration_impl.cc:121] loading stats sink configuration
	 configuration 'my-envoy-config.yaml' OK

Envoy logging
-------------


Envoy debugging
---------------
