Removed AWS-LC as a selectable SSL library, along with the ``--config=aws-lc-fips`` build
configuration. AWS-LC was previously the only way to build for the ppc64le architecture; ppc64le
builds should now use ``--config=openssl`` instead.
