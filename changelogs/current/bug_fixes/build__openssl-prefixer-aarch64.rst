Fixed ``--config=openssl`` builds on Linux ``aarch64`` hosts, where the ``compat/openssl``
``prefixer`` tool was linked against the ``x86_64`` ``libstdc++.a`` and failed with
``is incompatible with elf64-littleaarch64``. The hermetic GCC runtime used by the ``prefixer`` is
now selected by CPU.
