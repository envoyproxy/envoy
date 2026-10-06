Hardened the ``alternate_protocols_cache`` filter to avoid a crash when the upstream info or upstream
host is null, or when the upstream host address is not an IP address. Alternate protocols are now
cached only for IP endpoints.
