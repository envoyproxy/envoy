.. _install_sandboxes_default_config:

Default config
==============

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

This is a very simple sandbox to test the default config in the Envoy Docker container.

Step 1: Start the container
***************************

Change to the ``examples/default-config`` directory and bring up the docker composition.

.. code-block:: console

    $ pwd
    envoy/examples/default-config
    $ docker-compose build --pull
    $ docker-compose up -d
    $ docker-compose ps
    Name                       Command                        State   Ports
    --------------------------------------------------------------------------------------------------------------
    default-config_proxy_1     /docker-entrypoint.sh /usr ... Up      0.0.0.0:10000->10000/tcp,:::10000->10000/tcp


Step 2: Test proxying to the Envoy website
******************************************

.. code-block:: console

    $ curl -s https://localhost:10000 | grep "Envoy is an open source edge and service proxy, designed for cloud-native applications"
    Envoy is an open source edge and service proxy, designed for cloud-native applications


.. seealso::
   :ref:`Run Envoy with the demo configuration <start_quick_start_config>`
      Quick start guide to running Envoy.
