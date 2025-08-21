What are the requirements to run on Envoy on Windows?
=====================================================

.. include:: ../../_include/windows_support_ended.rst

Envoy was tested on Windows Server Core 2019 (Long-Term Servicing Channel). This corresponds to OS version 10.0.17763.1879. We have also tested a few more recent versions of Windows Server
and in general higher versions will be fully supported. For more info please see `Windows Server Core <https://hub.docker.com/_/microsoft-windows-servercore>`_.
To build Envoy from source you will need to have at least Windows 10 SDK, version 1803 (10.0.17134.12).
Earlier versions will not compile because the ``afunix.h`` header is not available.

There might be Envoy features that require newer versions of Windows. These features will explicitly document the required version.
