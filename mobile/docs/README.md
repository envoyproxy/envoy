# Envoy Mobile docs

Envoy Mobile's docs are generated using [Sphinx](http://www.sphinx-doc.org),
and are published
[here](https://envoy-mobile.github.io/docs/envoy-mobile/latest/index.html).

## Generating docs locally

To generate the docs locally, run:

```bash
./docs/build.sh
```

The output can be then be found in `generated/docs`.

## Updating the Envoy Mobile website and docs

The docs website is automatically updated with the latest docs when a commit is
merged to main. This is done via the [publish script](./publish.sh).
