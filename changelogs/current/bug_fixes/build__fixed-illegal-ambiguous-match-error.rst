Fixed ``Illegal ambiguous match`` error when building contrib targets with ``--config=aws-lc-fips``
on aarch64 by restricting the ``using_aws_lc`` branch of ``SELECTED_CONTRIB_EXTENSIONS`` to
``linux_x86_64``. Mirrors the approach taken by #32382 for ``boringssl_fips``.
