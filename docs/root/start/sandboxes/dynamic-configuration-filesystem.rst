.. _install_sandboxes_dynamic_config_fs:

Dynamic configuration (filesystem)
==================================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

   :ref:`jq <start_sandboxes_setup_jq>`
        Parse ``json`` output from the upstream echo servers.

This example walks through configuring Envoy using filesystem-based dynamic configuration.

It demonstrates how configuration provided to Envoy dynamically can be updated without
restarting the server.

Step 1: Start the proxy container
*********************************

Change directory to ``examples/dynamic-config-fs`` in the Envoy repository.

Build and start the containers.

This should also start two upstream ``HTTP`` echo servers, ``service1`` and ``service2``.

.. code-block:: console

    $ pwd
    envoy/examples/dynamic-config-fs
    $ docker-compose pull
    $ docker-compose up --build -d
    $ docker-compose ps

           Name                            Command                State                     Ports
    ------------------------------------------------------------------------------------------------------------------------
    dynamic-config-fs_proxy_1      /docker-entrypoint.sh /usr ... Up      0.0.0.0:10000->10000/tcp, 0.0.0.0:19000->19000/tcp
    dynamic-config-fs_service1_1   /bin/echo-server               Up      8080/tcp
    dynamic-config-fs_service2_1   /bin/echo-server               Up      8080/tcp

Step 2: Check web response
**************************

You should be able to make a request to port ``10000``, which will be served by ``service1``.

.. code-block:: console

   $ curl -s http://localhost:10000
   Request served by service1

   HTTP/2.0 GET /

   Host: localhost:10000
   User-Agent: curl/7.72.0
   Accept: */*
   X-Forwarded-Proto: http
   X-Request-Id: 6672902d-56ca-456c-be6a-992a603cab9a
   X-Envoy-Expected-Rq-Timeout-Ms: 15000

Step 3: Dump Envoy's ``dynamic_active_clusters`` config
*******************************************************

If you now dump the proxy’s :ref:`dynamic_active_clusters <envoy_v3_api_field_admin.v3.ClustersConfigDump.dynamic_active_clusters>`
configuration, you should see it is configured with the ``example_proxy_cluster`` pointing to ``service1``.

.. code-block:: console

   $ curl -s http://localhost:19000/config_dump | jq -r '.configs[1].dynamic_active_clusters'

.. literalinclude:: _include/dynamic-config-fs/response-config-active-clusters.json
   :language: json
   :emphasize-lines: 10, 18-19

Step 4: Replace ``cds.yaml`` inside the container to update upstream cluster
****************************************************************************

The example setup provides Envoy with two dynamic configuration files:

- :download:`configs/cds.yaml <_include/dynamic-config-fs/configs/cds.yaml>` to provide a :ref:`Cluster
  discovery service (CDS) <config_cluster_manager_cds>`.
- :download:`configs/lds.yaml <_include/dynamic-config-fs/configs/lds.yaml>` to provide a :ref:`Listener
  discovery service (LDS) <config_listeners_lds>`.

Edit ``cds.yaml`` inside the container and change the cluster address
from ``service1`` to ``service2``:

.. literalinclude:: _include/dynamic-config-fs/configs/cds.yaml
   :language: yaml
   :linenos:
   :lines: 6-13
   :lineno-start: 6
   :emphasize-lines: 7

You can do this using ``sed`` inside the container:

.. code-block:: console

   docker-compose exec -T proxy sed -i s/service1/service2/ /var/lib/envoy/cds.yaml

.. note::

   The above example uses ``sed -i``, which works as an inplace edit as ``sed`` does copy, edit and move in order to do this.

Step 5: Check Envoy uses updated configuration
**********************************************

Checking the web response again, the request should now be handled by ``service2``:

.. code-block:: console

   $ curl http://localhost:10000 | grep "served by"
   Request served by service2

Dumping the :ref:`dynamic_active_clusters <envoy_v3_api_field_admin.v3.ClustersConfigDump.dynamic_active_clusters>`,
the ``example_proxy_cluster`` should now be configured to proxy to ``service2``:

.. code-block:: console

   $ curl -s http://localhost:19000/config_dump | jq -r '.configs[1].dynamic_active_clusters'

.. literalinclude:: _include/dynamic-config-fs/response-config-active-clusters-updated.json
   :language: json
   :emphasize-lines: 10, 18-19

.. seealso::

   :ref:`Dynamic configuration (filesystem) quick start guide <start_quick_start_dynamic_filesystem>`
      Quick start guide to filesystem-based dynamic configuration of Envoy.

   :ref:`Envoy admin quick start guide <start_quick_start_admin>`
      Quick start guide to the Envoy admin interface.

   :ref:`Dynamic configuration (control plane) sandbox <install_sandboxes_dynamic_config_cp>`
      Configure Envoy dynamically with the Go Control Plane.
