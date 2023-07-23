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
         ...

   .. tab:: Docker (Linux Image)

      .. substitution-code-block:: console

         $ docker run --rm \
               envoyproxy/|envoy_docker_image| \
                   --version
         ...
   .. tab:: Docker (Windows Image)

      .. substitution-code-block:: powershell

         PS> docker run --rm
               'envoyproxy/|envoy_windows_docker_image|'
                  --version
         ...

.. _start_quick_start_help:

View the Envoy command line options
-----------------------------------

You can view the Envoy :ref:`command line options <operations_cli>` with the ``--help``
flag:

.. tabs::

   .. tab:: System

      .. code-block:: console

         $ envoy --help
         ...

   .. tab:: Docker (Linux Image)

      .. substitution-code-block:: console

         $ docker run --rm \
               envoyproxy/|envoy_docker_image| \
                   --help
         ...

   .. tab:: Docker (Windows Image)

      .. substitution-code-block:: powershell

         PS> docker run --rm
               'envoyproxy/|envoy_windows_docker_image|'
                    --help
         ...

.. _start_quick_start_config:

Run Envoy with the demo configuration
-------------------------------------

The ``-c`` or ``--config-path`` flag tells Envoy the path to its initial configuration.

Envoy will parse the config file according to the file extension, please see the
:option:`config path command line option <-c>` for further information.

.. tabs::

   .. tab:: System

      To start Envoy as a system daemon :download:`download the demo configuration <_include/envoy-demo.yaml>`, and start
      as follows:

      .. code-block:: console

         $ envoy -c envoy-demo.yaml
         ...

   .. tab:: Docker (Linux Image)

      You can start the Envoy Docker image without specifying a configuration file, and
      it will use the demo config by default.

      .. substitution-code-block:: console

         $ docker run --rm -it \
               -p 9901:9901 \
               -p 10000:10000 \
               envoyproxy/|envoy_docker_image|
         ...

      To specify a custom configuration you can mount the config into the container, and specify the path with ``-c``.

      Assuming you have a custom configuration in the current directory named ``envoy-custom.yaml``:

      .. substitution-code-block:: console

         $ docker run --rm -it \
               -v $(pwd)/envoy-custom.yaml:/envoy-custom.yaml \
               -p 9901:9901 \
               -p 10000:10000 \
               envoyproxy/|envoy_docker_image| \
                   -c /envoy-custom.yaml
         ...

   .. tab:: Docker (Windows Image)

      You can start the Envoy Docker image without specifying a configuration file, and
      it will use the demo config by default.

      .. substitution-code-block:: powershell

         PS> docker run --rm -it
               -p '9901:9901'
               -p '10000:10000'
               'envoyproxy/|envoy_windows_docker_image|'
         ...

      To specify a custom configuration you can mount the config into the container, and specify the path with ``-c``.

      Assuming you have a custom configuration in the current directory named ``envoy-custom.yaml``, from PowerShell run:

      .. substitution-code-block:: powershell

         PS> docker run --rm -it
               -v "$PWD\:`"C:\envoy-configs`""
               -p '9901:9901'
               -p '10000:10000'
               'envoyproxy/|envoy_windows_docker_image|'
                   -c 'C:\envoy-configs\envoy-custom.yaml'
         ...

Check Envoy is proxying on http://localhost:10000.

.. code-block:: console

   $ curl -v localhost:10000
   ...

You can exit the server with ``Ctrl-c``.

See the :ref:`admin quick start guide <start_quick_start_admin>` for more information about the Envoy admin interface.

.. _start_quick_start_override:

Override the default configuration
----------------------------------

You can provide an override configuration using :option:`--config-yaml` which will merge with the main
configuration.

This option can only be specified once.

Save the following snippet to ``envoy-override.yaml``:

.. code-block:: yaml

   admin:
     address:
       socket_address:
         address: 127.0.0.1
         port_value: 9902

.. warning::

  If you run Envoy inside a Docker container you may wish to use ``0.0.0.0``. Exposing the admin interface in this way may give unintended control of your Envoy server. Please see the :ref:`admin section <start_quick_start_admin_config>` for more information.

Next, start the Envoy server using the override configuration:

.. tabs::

   .. tab:: System

      On Linux/Mac: run:

      .. code-block:: console

         $ envoy -c envoy-demo.yaml --config-yaml "$(cat envoy-override.yaml)"
         ...

      On Windows run:

      .. code-block:: powershell

         $ envoy -c envoy-demo.yaml --config-yaml "$(Get-Content -Raw envoy-override.yaml)"
         ...

   .. tab:: Docker (Linux Image)

      .. substitution-code-block:: console

         $ docker run --rm -it \
               -p 9902:9902 \
               -p 10000:10000 \
               envoyproxy/|envoy_docker_image| \
                   -c /etc/envoy/envoy.yaml \
                   --config-yaml "$(cat envoy-override.yaml)"
         ...

   .. tab:: Docker (Windows Image)

      .. substitution-code-block:: powershell

         PS> docker run --rm -it
               -p '9902:9902'
               -p '10000:10000'
               'envoyproxy/|envoy_windows_docker_image|'
                  -c 'C:\ProgramData\envoy.yaml'
                  --config-yaml "$(Get-Content -Raw envoy-override.yaml)"
         ...

The Envoy admin interface should now be available on http://localhost:9902.

.. code-block:: console

   $ curl -v localhost:9902
   ...

.. note::

   When merging ``yaml`` lists (e.g. :ref:`listeners <envoy_v3_api_file_envoy/config/listener/v3/listener.proto>`
   or :ref:`clusters <envoy_v3_api_file_envoy/config/cluster/v3/cluster.proto>`) the merged configurations
   are appended.

   You cannot therefore use an override file to change the configurations of previously specified
   :ref:`listeners <envoy_v3_api_file_envoy/config/listener/v3/listener.proto>` or
   :ref:`clusters <envoy_v3_api_file_envoy/config/cluster/v3/cluster.proto>`

Validating your Envoy configuration
-----------------------------------

You can start Envoy in :option:`validate mode <--mode>`.

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

   .. tab:: Docker (Linux Image)

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

   .. tab:: Docker (Windows Image)

      .. substitution-code-block:: powershell

         PS> docker run --rm -it
               -v "$PWD\:`"C:\envoy-configs`""
               -p '9901:9901'
               -p '10000:10000'
               'envoyproxy/|envoy_windows_docker_image|'
                  --mode validate
                  -c 'C:\envoy-configs\my-envoy-config.yaml'

         configuration 'my-envoy-config.yaml' OK

Envoy logging
-------------

By default Envoy system logs are sent to ``/dev/stderr``.

This can be overridden using :option:`--log-path`.

.. tabs::

   .. tab:: System

      .. code-block:: console

         $ mkdir logs
         $ envoy -c envoy-demo.yaml --log-path logs/custom.log

   .. tab:: Docker (Linux Image)

      .. substitution-code-block:: console

         $ mkdir logs
         $ chmod go+rwx logs/
         $ docker run --rm -it \
               -p 10000:10000 \
               -v $(pwd)/logs:/logs \
               envoyproxy/|envoy_docker_image| \
                   -c /etc/envoy/envoy.yaml \
                   --log-path logs/custom.log

   .. tab:: Docker (Windows Image)

      .. substitution-code-block:: powershell

            PS> mkdir logs
            PS> docker run --rm -it
                  -p '10000:10000'
                  -v "$PWD\logs\:`"C:\logs`""
                  'envoyproxy/|envoy_windows_docker_image|'
                     -c 'C:\ProgramData\envoy.yaml'
                     --log-path 'C:\logs\custom.log'

      .. note::

         Envoy on a Windows system Envoy will output to ``CON`` by default.

         This can also be used as a logging path when configuring logging.

:ref:`Access log <arch_overview_access_logs>` paths can be set for the
:ref:`admin interface <start_quick_start_admin>`, and for configured
:ref:`listeners <envoy_v3_api_file_envoy/config/listener/v3/listener.proto>`.

The :download:`demo configuration <_include/envoy-demo.yaml>` is configured with a
:ref:`listener <envoy_v3_api_file_envoy/config/listener/v3/listener.proto>` that logs access
to ``/dev/stdout``:

.. literalinclude:: _include/envoy-demo.yaml
   :language: yaml
   :linenos:
   :lineno-start: 12
   :lines: 12-22
   :emphasize-lines: 4-7

The default configuration in the Envoy Docker container also logs access in this way.

Logging to ``/dev/stderr`` and ``/dev/stdout`` for system and access logs respectively can
be useful when running Envoy inside a container as the streams can be separated, and logging requires no
additional files or directories to be mounted.

Some Envoy :ref:`filters and extensions <api-v3_config>` may also have additional logging capabilities.

Envoy can be configured to log to :ref:`different formats <config_access_log>`, and to
:ref:`different outputs <api-v3_config_accesslog>` in addition to files and ``stdout/err``.

Envoy networking
----------------

By default Envoy can use both IPv4 and IPv6 networks.

If your environment does not support IPv6 you should disable it.

This may be the case when using Docker on a non-linux host (see here for more information regarding
`IPv6 support in Docker <https://docs.docker.com/config/daemon/ipv6/>`_).

You can disable IPv6 by setting the ``dns_lookup_family`` to ``V4_ONLY`` in your configuration as follows:

.. literalinclude:: _include/envoy-demo.yaml
   :language: yaml
   :linenos:
   :lineno-start: 34
   :lines: 34-46
   :emphasize-lines: 5
   :caption: :download:`envoy-demo.yaml <_include/envoy-demo.yaml>`

Debugging Envoy
---------------

The log level for Envoy system logs can be set using the :option:`-l or --log-level <--log-level>` option.

The available log levels are:

- ``trace``
- ``debug``
- ``info``
- ``warning/warn``
- ``error``
- ``critical``
- ``off``

The default  is ``info``.

You can also set the log level for specific components using the :option:`--component-log-level` option.

The following example inhibits all logging except for the ``upstream`` and ``connection`` components,
which are set to ``debug`` and ``trace`` respectively.

.. tabs::

   .. tab:: System

      .. code-block:: console

         $ envoy -c envoy-demo.yaml -l off --component-log-level upstream:debug,connection:trace
         ...

   .. tab:: Docker (Linux Image)

      .. substitution-code-block:: console

         $ docker run --rm -d \
               -p 9901:9901 \
               -p 10000:10000 \
               envoyproxy/|envoy_docker_image| \
                   -c /etc/envoy/envoy.yaml \
                   -l off \
                   --component-log-level upstream:debug,connection:trace
         ...

   .. tab:: Docker (Windows Image)

      .. substitution-code-block:: powershell

            PS> mkdir logs
            PS> docker run --rm -it
                  -p '10000:10000'
                  envoyproxy/|envoy_windws_docker_image|
                     -c 'C:\ProgramData\envoy.yaml'
                     -l off
                     --component-log-level 'upstream:debug,connection:trace'
            ...

.. tip::

   See ``ALL_LOGGER_IDS`` in :repo:`logger.h </source/common/common/logger.h#L29>` for a list of components.
