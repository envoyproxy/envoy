How fast is Envoy on Windows?
=============================

.. include:: ../../_include/windows_support_ended.rst

Everything that is mentioned in :ref:`How fast is Envoy? <faq_how_fast_is_envoy>` applies to Windows. We have
done some work to improve the event loop on Windows. That being said, we have observed that the tail performance of Envoy on Windows
tends to be worse compared to Linux, especially when TLS is involved.

We are actively investigating performance and aim to improve on a continuous basis.
