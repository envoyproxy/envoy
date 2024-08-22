Can I run Envoy on Windows under SCM?
=====================================

.. include:: ../../_include/windows_support_ended.rst

.. note::

    This feature is still in Experimental state.


You can start Envoy as Windows Service that is managed under `Windows Service Control Manager <https://docs.microsoft.com/en-us/windows/win32/services/using-services/>`_.
First, you need to create the service. Assuming you have a custom configuration in the current directory named ``envoy-custom.yaml``. After you create the service you
can start it.

From an **administrator** prompt run the following commands (note that you need replace C:\EnvoyProxy\ with the path to the envoy.exe binary and the config file):

.. code-block:: console

      > sc create EnvoyProxy binpath="C:\EnvoyProxy\envoy.exe --config-path C:\EnvoyProxy\envoy-demo.yaml" start=auto depend=Tcpip/Afd
         [SC] CreateService SUCCESS
      > sc start EnvoyProxy
         SERVICE_NAME: envoyproxy
            TYPE               : 10  WIN32_OWN_PROCESS
            STATE              : 2  START_PENDING
                                    (NOT_STOPPABLE, NOT_PAUSABLE, IGNORES_SHUTDOWN)
            WIN32_EXIT_CODE    : 0  (0x0)
            SERVICE_EXIT_CODE  : 0  (0x0)
            CHECKPOINT         : 0x0
            WAIT_HINT          : 0x7d0
            PID                : 3924
            FLAGS              :
      > sc query EnvoyProxy
         SERVICE_NAME: envoyproxy
            TYPE               : 10  WIN32_OWN_PROCESS
            STATE              : 4  RUNNING
                                    (STOPPABLE, NOT_PAUSABLE, ACCEPTS_SHUTDOWN)
            WIN32_EXIT_CODE    : 0  (0x0)
            SERVICE_EXIT_CODE  : 0  (0x0)
            CHECKPOINT         : 0x0
            WAIT_HINT          : 0x0
   ...


Use `sc.exe <https://docs.microsoft.com/en-us/windows-server/administration/windows-commands/sc-create/>`_ to configure the service startup and error handling.

.. tip::

   The output of ``sc query envoyproxy`` contains the exit code of Envoy Proxy. In case the arguments are invalid we set it to ``E_INVALIDARG``. For more information
   Envoy is reporting startup failures with error messages on Windows Event Viewer.
