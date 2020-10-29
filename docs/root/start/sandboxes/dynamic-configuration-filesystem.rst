.. _install_sandboxes_dynamic_config_cp:

Dynamic configuration (filesystem)
==================================

This example walks through configuring Envoy using filesystem-based dynamic configuration.

It demonstrates how configuration provided to Envoy dynamically can be updated without
restarting the server.

.. include:: _include/docker-env-setup.rst

Change directory to ``examples/dynamic-config-fs`` in the Envoy repository.

Step 3: Start the proxy container
*********************************

.. note::

   If you are running on a system with strict ``umask`` you will need to ``chmod`` the dynamic config
   files which are mounted into the container:

   .. code-block:: console

      $ umask
      027
      $ pwd
      envoy/examples/dynamic-config-fs
      $ chmod go+r configs/*
      $ chmod go+x configs

First build the containers and start the ``proxy`` container.

This should also start two backend ``HTTP`` echo servers, ``service1`` and ``service2``.

.. code-block:: console

    $ pwd
    envoy/examples/dynamic-config-fs
    $ docker-compose build --pull
    $ docker-compose up -d proxy
    $ docker-compose ps

           Name                            Command                State                     Ports
    ------------------------------------------------------------------------------------------------------------------------
    dynamic-config-fs_proxy_1      /docker-entrypoint.sh /usr ... Up      0.0.0.0:10000->10000/tcp, 0.0.0.0:19000->19000/tcp
    dynamic-config-fs_service1_1   /bin/echo-server               Up      8080/tcp
    dynamic-config-fs_service2_1   /bin/echo-server               Up      8080/tcp

Step 4: Check initial config and web response
*********************************************

.. code-block:: console

   $ curl -s http://localhost:10000

.. code-block:: console

   $ curl -s http://localhost:19000/config_dump jq -r '.configs[1].dynamic_active_clusters'

.. literalinclude:: _include/dynamic-config-fs/response-config-active-clusters.json
   :language: json
   :emphasize-lines: 3, 11, 19-20

Step 5: Edit ``cds.yaml`` file to update upstream cluster
*********************************************************

:download:`cds.yaml <_include/dynamic-config-fs/configs/cds.yaml>`

.. literalinclude:: _include/dynamic-config-fs/configs/cds.yaml
   :language: yaml
   :linenos:
   :lines: 9-16
   :emphasize-lines: 5

Step 6: Check web response again
********************************

.. code-block:: console

   $ curl http://localhost:10000 | grep "served by"
   Request served by service2


Step 7: Check configs again
***************************

.. code-block:: console

   $ curl -s http://localhost:19000/config_dump jq -r '.configs[1].dynamic_active_clusters'

.. literalinclude:: _include/dynamic-config-fs/response-config-active-clusters.json
   :language: json
   :emphasize-lines: 3, 11, 19-20
