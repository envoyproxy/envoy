.. _install_sandboxes_wasm_filter:

WASM C++ filter
===============

This sandbox demonstrates a basic C++ Wasm filter which injects content into the body of an ``HTTP`` response, and adds
and updates some headers.

It also takes you through the steps required to build your own C++ Wasm filter, and run it with Envoy.

.. include:: _include/docker-env-setup.rst

Step 3: Start all of our containers
***********************************

.. note::

   The provided Wasm binary was compiled for the ``x86_64`` architecture. If you would like to use this sandbox
   with the ``arm64`` architecture, skip to Step 5.

First lets start the containers - an Envoy proxy which uses a Wasm Filter, and a backend which echos back our request.

Change to the ``examples/wasm-cc`` folder in the Envoy repo, and start the composition:

.. code-block:: console

    $ pwd
    envoy/examples/wasm-cc
    $ docker-compose build --pull
    $ docker-compose up -d
    $ docker-compose ps

	Name                     Command                State             Ports
    -----------------------------------------------------------------------------------------------
    wasm_proxy_1         /docker-entrypoint.sh /usr ... Up      10000/tcp, 0.0.0.0:8000->8000/tcp
    wasm_web_service_1   node ./index.js                Up

Step 4: Check web response
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

Step 5: Compile the updated filter
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

Step 6: Edit the Dockerfile and restart the proxy
*************************************************

Edit the ``Dockerfile-proxy`` recipe provided in the example to use the updated binary you created in step 5.

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

Step 7: Check the proxy has been updated
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
