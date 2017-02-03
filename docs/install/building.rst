Building
========

The Envoy build system uses cmake. In order to ease initial building and for a quick start, we
provide an Ubuntu 14 based docker container that has everything needed inside of it to build
and *statically link* envoy. The following command will build the server.

.. code-block:: console

  docker run -t -i -v <SOURCE_DIR>:/source lyft/envoy-build:latest /bin/bash -c "cd /source && ci/do_ci.sh server_only"

See :repo:`ci/do_ci.sh` for other possible targets (to run tests, etc.).

In order to build manually, cmake is used like so:

.. code-block:: console

  mkdir build
  cd build
  cmake ..
  make

Note that in order for the above raw build to work, cmake variables will need to be configured so
that the envoy build can find all of the needed third party dependencies (other variables are also
available to turn on debug builds, address sanitizer, etc.).

* :repo:`CMakeLists.txt`
* :repo:`common.cmake`
* :repo:`thirdparty.cmake`

(Experimental) Building with Bazel_

Follow the instruction_ to install Bazel, then run following command to build Envoy:

.. code-block:: console

  bazel build //:envoy

The built binary will be at `bazel-bin/envoy`

To run tests, run following command:

.. code-block:: console

  bazel test //:envoy-test

Note not all tests pass with Bazel yet.

.. _Bazel: https://bazel.build/
.. _instruction: https://bazel.build/versions/master/docs/install.html
