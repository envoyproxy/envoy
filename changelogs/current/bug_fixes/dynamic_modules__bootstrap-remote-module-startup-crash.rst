dynamic modules: fixed a startup crash where a bootstrap extension configured with a remote module
source and ``nack_on_cache_miss`` dereferenced the cluster manager before it was created. Bootstrap
extensions now reject a remote module source at config load, since the cluster manager needed to
fetch it does not exist yet at bootstrap time.
