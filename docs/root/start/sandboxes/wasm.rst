.. _install_sandboxes_wasm_filter:

WASM filter
===========

This sandbox demonstrates a basic Wasm filter which injects content into the body of an ``HTTP`` request, and adds
and updates some headers.

It also takes you through the steps required to build your own Wasm filter, and run it with Envoy.

Running the Sandbox
~~~~~~~~~~~~~~~~~~~

.. include:: _include/docker-env-setup.rst

Step 3: Start all of our containers
***********************************

First lets start the containers - an Envoy proxy which uses a Wasm Filter, and a backend which echos back our request.

.. code-block:: console

    $ pwd
    envoy/examples/wasm
    $ docker-compose build --pull
    $ docker-compose up -d
    $ docker-compose ps

	Name                     Command                State             Ports
    -----------------------------------------------------------------------------------------------
    wasm_proxy_1         /docker-entrypoint.sh /usr ... Up      10000/tcp, 0.0.0.0:8000->8000/tcp
    wasm_web_service_1   node ./index.js                Up      0.0.0.0:9000->9000/tcp

Step 3: Check web response
**************************

The Wasm filter should inject "Hello, world" at the end of the response body.

.. code-block:: console

   $ curl -s http://localhost:8000 | grep "Hello, world"
   }Hello, world

The filter also sets the ``content-type`` header to ``text/plain``, and adds a custom Wasm header ``x-wasm-custom``.

.. code-block:: console

   $ curl -v http://localhost:8000 | grep "content-type: "
   content-type: text/plain; charset=utf-8

   $ curl -v http://localhost:8000 | grep "x-wasm-custom: "
   x-wasm-custom: FOO


Step 4: Compile updated filter
******************************

There are two source code files for the Wasm filter.

The first (:download:`envoy_filter_http_wasm_example.cc <_include/wasm/envoy_filter_http_wasm_example.cc>`) is the source code for
the included prebuilt binary.

The second (:download:`envoy_filter_http_wasm_updated_example.cc <_include/wasm/envoy_filter_http_wasm_updated_example.cc>`) makes
a few changes to the original.

The following diff shows the changes that have been made:

.. literalinclude:: _include/wasm/envoy_filter_http_wasm_updated_example.cc
    :diff: _include/wasm/envoy_filter_http_wasm_example.cc

Stop the proxy server and compile the Wasm binary with the updated file:

.. code-block:: console

   $ docker-compose stop proxy
   $ docker-compose -f docker-compose-wasm.yaml up --remove-orphans wasm_compile_update


Step 4: Check the updated web response
**************************************

Edit the Docker recipe to use the updated binary

edit...

Restart the proxy server

.. code-block:: console

   $ docker-compose up --build -d proxy


The Wasm filter should now inject "Hello, Wasm world" into the end of the response body.

.. code-block:: console

   $ curl -s http://localhost:8000 | grep "Hello, world"
   }Hello, Wasm world

The content-type and custom Wasm headers have also changed

.. code-block:: console

   $ curl -v http://localhost:8000 | grep "content-type: "
   content-type: text/html; charset=utf-8

   $ curl -v http://localhost:8000 | grep "x-wasm-custom: "
   x-wasm-custom: BAR
