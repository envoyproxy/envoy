.. _install_sandboxes_wasm_filter:

Wasm C++ filter
===============

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

.. sidebar:: Compatibility

   The provided Wasm binary was compiled for the ``x86_64`` architecture. If you would like to use this sandbox
   with the ``arm64`` architecture, change directory to ``examples/wasm-cc`` and skip to Step 3.

This sandbox demonstrates a basic :ref:`Envoy Wasm filter <config_http_filters_wasm>` written in C++ which injects
content into the body of an ``HTTP`` response, and adds and updates some headers.

It also takes you through the steps required to build your own C++ :ref:`Wasm filter <config_http_filters_wasm>`,
and run it with Envoy.

Step 1: Start all of our containers
***********************************

First lets start the containers - an Envoy proxy which uses a Wasm Filter, and a backend which echos back our request.

Change to the ``examples/wasm-cc`` folder in the Envoy repo, and start the composition:

.. code-block:: console

    $ pwd
    envoy/examples/wasm-cc
    $ docker-compose pull
    $ docker-compose up --build -d
    $ docker-compose ps

        Name                     Command                State             Ports
    -----------------------------------------------------------------------------------------------
    wasm_proxy_1         /docker-entrypoint.sh /usr ... Up      10000/tcp, 0.0.0.0:8000->8000/tcp
    wasm_web_service_1   node ./index.js                Up

Step 2: Check web response
**************************

The Wasm filter should inject "Hello, world" at the end of the response body when you make a request to the proxy.

.. code-block:: console

   $ curl -s http://localhost:8000 | grep "Hello, world"
   }Hello, world

The filter also sets the ``content-type`` header to ``text/plain``, and adds a custom ``x-wasm-custom`` header.

.. code-block:: console

   $ curl -v http://localhost:8000 | grep "content-type: "
   content-type: text/plain; charset=utf-8

   $ curl -v http://localhost:8000 | grep "x-wasm-custom: "
   x-wasm-custom: FOO

Step 3: Compile the updated filter
**********************************

There are two source code files provided for the Wasm filter.

:download:`envoy_filter_http_wasm_example.cc <_include/wasm-cc/envoy_filter_http_wasm_example.cc>` provides the source code for
the included prebuilt binary.

:download:`envoy_filter_http_wasm_updated_example.cc <_include/wasm-cc/envoy_filter_http_wasm_updated_example.cc>` makes a few
changes to the original.

The following diff shows the changes that have been made:

.. literalinclude:: _include/wasm-cc/envoy_filter_http_wasm_updated_example.cc
    :diff: _include/wasm-cc/envoy_filter_http_wasm_example.cc

.. warning::

   These instructions for compiling an updated Wasm binary use the
   `envoyproxy/envoy-build-ubuntu <https://hub.docker.com/r/envoyproxy/envoy-build-ubuntu/tags>`_ image.
   You will need 4-5GB of disk space to accommodate this image.

Export ``UID`` from your host system. This will ensure that the binary created inside the build container has the same permissions
as your host user:

.. code-block:: console

   $ export UID

.. note::

   The build composition is designed to work in a similar way to the ``./ci/run_envoy_docker.sh`` command in the Envoy repo.

   Bazel temporary artefacts are created in ``/tmp/envoy-docker-build`` with the uid taken from the ``UID`` env var.

Stop the proxy server and compile the Wasm binary with the updated code:

.. code-block:: console

   $ docker-compose stop proxy
   $ docker-compose -f docker-compose-wasm.yaml up --remove-orphans wasm_compile_update

The compiled binary should now be in the ``lib`` folder.

.. code-block:: console

   $ ls -l lib
   total 120
   -r-xr-xr-x 1 root root 59641 Oct 20 00:00 envoy_filter_http_wasm_example.wasm
   -r-xr-xr-x 1 root root 59653 Oct 20 10:16 envoy_filter_http_wasm_updated_example.wasm

Step 4: Edit the Dockerfile and restart the proxy
*************************************************

Edit the ``Dockerfile-proxy`` recipe provided in the example to use the updated binary you created in step 3.

Find the ``COPY`` line that adds the Wasm binary to the image:

.. literalinclude:: _include/wasm-cc/Dockerfile-proxy
   :language: dockerfile
   :emphasize-lines: 3
   :linenos:

Replace this line with the following:

.. code-block:: dockerfile

   COPY ./lib/envoy_filter_http_wasm_updated_example.wasm /lib/envoy_filter_http_wasm_example.wasm

Now, rebuild and start the proxy container.

.. code-block:: console

   $ docker-compose up --build -d proxy

Step 5: Check the proxy has been updated
****************************************

The Wasm filter should instead inject "Hello, Wasm world" at the end of the response body.

.. code-block:: console

   $ curl -s http://localhost:8000 | grep "Hello, Wasm world"
   }Hello, Wasm world

The ``content-type`` and ``x-wasm-custom`` headers should also have changed

.. code-block:: console

   $ curl -v http://localhost:8000 | grep "content-type: "
   content-type: text/html; charset=utf-8

   $ curl -v http://localhost:8000 | grep "x-wasm-custom: "
   x-wasm-custom: BAR

.. seealso::

   :ref:`Envoy Wasm filter <config_http_filters_wasm>`
      Further information about the Envoy Wasm filter.

   :ref:`Envoy Wasm API(V3) <envoy_v3_api_msg_extensions.filters.http.wasm.v3.Wasm>`
      The Envoy Wasm API - version 3.

   `Proxy Wasm C++ SDK <https://github.com/proxy-wasm/proxy-wasm-cpp-sdk>`_
      WebAssembly for proxies (C++ SDK)
