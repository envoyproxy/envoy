# Developer-local docs build

```bash
./docs/build.sh
```

The output can be found in `generated/docs`. By default configuration examples are going to be validated during build. 
To disable validation, set `SPHINX_SKIP_CONFIG_VALIDATION` environment variable to `true`:

```bash
SPHINX_SKIP_CONFIG_VALIDATION=true docs/build.sh
```


# How the Envoy website and docs are updated

1. The docs are published to [docs/envoy/latest](https://github.com/envoyproxy/envoyproxy.github.io/tree/master/docs/envoy/latest)
   on every commit to master. This process is handled by CircleCI with the
  [`publish.sh`](https://github.com/envoyproxy/envoy/blob/master/docs/publish.sh) script.

2. The docs are published to [docs/envoy](https://github.com/envoyproxy/envoyproxy.github.io/tree/master/docs/envoy)
   in a directory named after every tagged commit in this repo. Thus, on every tagged release there
   are snapped docs.
