Fixed ``Illegal ambiguous match`` error when building contrib targets with ``--config=boringssl-fips``
on aarch64 by restricting the ``using_boringssl_fips`` branch of ``SELECTED_CONTRIB_EXTENSIONS`` to
``linux_x86_64``. Mirrors the approach taken by #44661 for ``aws-lc``.
