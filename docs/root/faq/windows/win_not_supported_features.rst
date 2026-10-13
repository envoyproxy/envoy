Which Envoy features are not supported on Windows?
==================================================

.. include:: ../../_include/windows_support_ended.rst

The vast majority of Envoy features are supported on Windows. There are few exceptions that are documented explicitly.
The most notable features that are not supported on Windows are:

* :ref:`Watchdog <watchdog_api_reference>`
* :ref:`Tracers <http_tracers>`
* :ref:`Original Src HTTP Filter <arch_overview_ip_transparency_original_src_http>`.
* :ref:`Hot restart <arch_overview_hot_restart>`
* :ref:`Signed Exchange Filter <config_http_filters_sxg>`
* :ref:`VCL Socket Interface <config_sock_interface_vcl>`

There are certain Envoy features that require newer versions of Windows. These features explicitly document the required version.

We will continue adding support for the missing features over time and define a roadmap to bring platform support to parity with Linux.
