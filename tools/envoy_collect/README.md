# Collect Envoy stats/log/profile.

The `envoy_collect.py` wrapper script supports gathering up a bundle of useful artifacts from Envoy's
execution for the purpose of performance profiling or debugging.

## Debugging

To collect verbose logs, stats and other accessible data from the admin endpoint in a tarball,
invoke `/path/to/envoy-static` as follows under the `envoy_collect.py` wrapper:

```
envoy_collect.py --envoy-binary /path/to/envoy-static --output-path /path/to/debug.tar -c \
  /path/to/envoy-config.json <other Envoy args...>
```

Envoy will run as normal, but when interrupted by SIGINT (e.g. via `kill -s INT` or ctrl-c on
stdin), it will write to `/path/to/debug.tar` a dump of various logs and the admin endpoint
handlers. The wrapper configures Envoy for maximum logging verbosity.

This tarball may be useful to attach to issues when reporting. However, a high degree of caution is
recommended here, as the logs are verbose and will reveal low level traffic details. It is **NOT**
recommended to attach this to a GitHub issue if there are any privacy concerns whatsoever, otherwise
the data should be manually sanitized prior to posting in public view.

## Performance

To collect a performance profile, as well as the non-performance impacting logs and stats,
invoke `/path/to/envoy-static` as follows under the `envoy_collect.py` wrapper:

```
envoy_collect.py --performance --envoy-binary /path/to/envoy-static --output-path /path/to/debug.tar -c \
  /path/to/envoy-config.json <other Envoy args...>
```

This will run Envoy under `perf record` and include a `perf.data` file in the tarball, suitable
for later analysis with `perf report` or flamegraph generation.
