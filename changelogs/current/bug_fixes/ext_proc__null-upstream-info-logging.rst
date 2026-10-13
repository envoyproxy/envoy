Hardened the ``ext_proc`` filter to avoid a crash when logging stream info for a stream whose
upstream info is null. The upstream host is now recorded only when upstream info is present.
