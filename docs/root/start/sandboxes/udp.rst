.. _install_sandboxes_udp:

User Datagram Protocol (``UDP``)
================================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

   :ref:`netcat <start_sandboxes_setup_netcat>`
        Used to send ``UDP`` packets.

This sandbox provides a very simple example of Envoy proxying ``UDP``.

It also demonstrates ``UDP`` traffic stats provided by the Envoy admin endpoint.

Step 1: Build the sandbox
*************************

Change directory to ``examples/udp`` in the Envoy repository.

Start the Docker composition:

.. code-block:: console

  $ pwd
  envoy/examples/udp
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

      Name                 Command                   State      Ports
  -----------------------------------------------------------------------------------------------------------------------
  udp_envoy-udp_1     /docker-entrypoint.sh /usr ... Up     10000/tcp, 0.0.0.0:10000->10000/udp, 0.0.0.0:10001->10001/tcp
  udp_service-udp_1   python -u /udplistener.py      Up     5005/tcp, 5005/udp

Envoy should proxy ``UDP`` on port ``10000`` to an upstream server listening on port ``5005``.

Envoy also provides an admin endpoint listening on port ``10001``.

Step 2: Send some ``UDP`` messages
**********************************

You can use ``netcat`` to send packets to the upstream server, proxied by Envoy:

.. code-block:: console

   echo -n HELO | nc -4u -w1 127.0.0.1 10000
   echo -n OLEH | nc -4u -w1 127.0.0.1 10000

Step 3: Check the logs of the upstream ``UDP`` listener server
**************************************************************

Checking the logs of the upstream server you should see the packets that you sent:

.. code-block:: console

   $ docker-compose logs service-udp
   Attaching to udp_service-udp_1
   service-udp_1  | Listening on UDP port 5005
   service-udp_1  | HELO
   service-udp_1  | OLEH

Step 4: View the Envoy admin ``UDP`` stats
******************************************

You can view the ``UDP``-related stats provided by the Envoy admin endpoint.

For example, to view the non-zero stats:

.. code-block:: console

   $ curl -s http://127.0.0.1:10001/stats | grep udp | grep -v "\: 0"
   cluster.service_udp.default.total_match_count: 1
   cluster.service_udp.max_host_weight: 1
   cluster.service_udp.membership_change: 1
   cluster.service_udp.membership_healthy: 1
   cluster.service_udp.membership_total: 1
   cluster.service_udp.udp.sess_tx_datagrams: 2
   cluster.service_udp.update_attempt: 1
   cluster.service_udp.update_success: 1
   cluster.service_udp.upstream_cx_tx_bytes_total: 8
   udp.service.downstream_sess_active: 2
   udp.service.downstream_sess_rx_bytes: 8
   udp.service.downstream_sess_rx_datagrams: 2
   udp.service.downstream_sess_total: 2
   cluster.service_udp.upstream_cx_connect_ms: No recorded values
   cluster.service_udp.upstream_cx_length_ms: No recorded values
