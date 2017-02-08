Building
========

The Envoy build system uses cmake. In order to ease initial building and for a quick start, we
provide an Ubuntu 14 based docker container that has everything needed inside of it to build
and *statically link* envoy, see the [developer documentation](https://github.com/lyft/envoy/tree/master/ci).

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
