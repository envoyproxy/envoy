.. _start_sandboxes:

Sandboxes
---------

.. sidebar:: Contributing

   If there are other sandboxes you would like to see demonstrated here
   please
   `open a ticket on github <https://github.com/envoyproxy/envoy/issues/new?assignees=&labels=enhancement%2Ctriage&template=feature_request.md&title=>`_.

   :repo:`The Envoy project welcomes contributions <CONTRIBUTING.md>` and would be happy to review a
   `Pull Request <https://github.com/envoyproxy/envoy/pulls>`_ with the necessary changes
   should you be able to create one.

   :repo:`See the sandbox developer documentation <examples/DEVELOPER.md>` for more information about
   creating your own sandbox.

.. sidebar:: Compatibility

   As the examples use the pre-built :ref:`Envoy Docker images <install_binaries>` they should work
   on the following architectures:

   - x86_64
   - ARM 64

   Some of the examples may use pre-built (x86) binaries and will therefore have more limited
   compatibility.

We have created a number of sandboxes using `Docker Compose <https://docs.docker.com/compose/>`_
that set up environments to test out Envoy's features and show sample configurations.

These can be used to learn Envoy and model your own configurations.


Before you begin you will need to install the sandbox environment.

.. toctree::
    :maxdepth: 2

    setup

The following sandboxes are available:

.. include:: toctree.rst
