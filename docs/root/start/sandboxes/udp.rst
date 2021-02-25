.. _install_sandboxes_udp:

User Datagram Protocol (``UDP``)
================================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`netcat <start_sandboxes_setup_netcat>`
	Used to send ``UDP`` packets.


Step 1: Build the sandbox
*************************

Change directory to ``examples/udp`` in the Envoy repository.

.. code-block:: console

  $ pwd
  envoy/examples/udp
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

      Name                 Command                   State      Ports
  ----------------------------------------------------------------------------------------------
  udp_service-udp_1   python -u /udplistener.py      Up      5005/tcp, 5005/udp
  udp_testing_1       /docker-entrypoint.sh /usr ... Up      10000/tcp, 0.0.0.0:10000->10000/udp
