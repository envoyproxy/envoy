.. _install_sandboxes_wasm_filter:

WASM filter
===========

This sandbox demonstrates a basic Wasm filter which injects content into the body of the request, adds and updates some
headers.

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

The Wasm filter should inject "Hello, world" into the end of the response body.

.. code-block:: console

   $ curl -s http://localhost:8000 | grep "Hello, world"
   }Hello, world

The filter also sets the location header to ``envoy-wasm``, and adds a custom Wasm header ``x-wasm-custom``.

.. code-block:: console

   $ curl -v http://localhost:8000 | grep "location: "
   location: envoy-wasm

   $ curl -v http://localhost:8000 | grep "x-wasm-custom: "
   x-wasm-custom: FOO


Step 4: Compile updated filter
******************************

Stop the proxy server and recompile the Wasm binary:

.. code-block:: console

   $ docker-compose stop proxy
   $ docker-compose -f docker-compose-wasm.yaml up --remove-orphans wasm_compile_update


Step 4: Check the updated web response
**************************************

Edit the Docker recipe to use the updated binary

Restart the proxy server

.. code-block:: console

   $ docker-compose up --build -d proxy


The Wasm filter should now inject "Hello, Wasm world" into the end of the response body.

.. code-block:: console

   $ curl -s http://localhost:8000 | grep "Hello, world"
   }Hello, Wasm world

The location and custom Wasm headers have also changed

.. code-block:: console

   $ curl -v http://localhost:8000 | grep "location: "
   location: updated-envoy-wasm

   $ curl -v http://localhost:8000 | grep "x-wasm-custom: "
   x-wasm-custom: BAR
