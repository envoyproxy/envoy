
# ![{{ envoy.title }}]({{ envoy.logo }}) {{ envoy.title }}

## Quick reference

- **Maintained by**:

    [{{ envoy.maintained_by }}]({{ envoy.project_url }}).

- **Where to get help**:

    [Official documentation]({{ envoy.docs_url }}), [the Envoy community Slack]({{ envoy.slack_url}}).

## Supported tags and respective `Dockerfile` links

{% for version in stable_versions %}
- [v{{ version }}-latest](https://github.com/envoyproxy/envoy/blob/release/v{{ version }}/ci/Dockerfile-envoy)
{% endfor %}
- [dev](https://github.com/envoyproxy/envoy/blob/release/main/ci/Dockerfile-envoy)


## Quick reference (cont.)

- Where to file issues:
  {{ envoy.issues_url }}

- Supported architectures:
{% for arch in architectures %}
  `{{ arch }}`
{% endfor %}

## Image variants

For stable Envoy versions images are created for the version and the latest of that minor version.

For example, if the latest version in the `1.73.x` series is `1.73.7` then images are created for:

- `envoyproxy/envoy:v1.73.7`
- `envoyproxy/envoy:v1.73-latest`

A similar strategy is used to create images for each of the versioned variants.

### `envoyproxy/envoy:<version>`

These images contain just the core Envoy binary built upon an Ubuntu base image.

### `envoyproxy/envoy:contrib-<version>`

These images contain the Envoy binary built with all contrib extensions.

### `envoyproxy/envoy:distroless-<version>`

These images contain just the core Envoy binary built upon a [distroless](https://github.com/GoogleContainerTools/distroless)
(`nonroot`/`nossl`) base image.

These images are the most efficient and secure way to deploy Envoy in a container.

### `envoyproxy/envoy:tools-<version>`

These images contain tools that are separate from the proxy binary but are useful in supporting systems
such as CI, configuration generation pipelines, etc

### `envoyproxy/envoy:debug-<version>`/`envoyproxy/envoy:<variant>-debug-<version>`

These images are built for each of the variants, but with an Envoy binary containing debug symbols.

### `envoyproxy/envoy:dev`/`envoyproxy/envoy:dev-<SHA>`/`envoyproxy/envoy:<variant>-dev`/`envoyproxy/envoy:<variant>-dev-<SHA>`

Development images are created from the `main` branch by Envoy's continuous integration, and are tagged with the `dev` suffix.

Images are created for each of the versioned variants.

For each variant, images are tagged with just the `dev` suffix and with the `dev-<SHA>` suffix, where the `SHA` is the commit
in Envoy `main` from which it was created.

For example, after a build at commit `7c1c4a0e`, an image will be created for `envoyproxy/envoy:dev-7c1c4a0e10a7a0771ac06ce8cf8fa2c6ce86281b`
and the image `envoyproxy/envoy:dev` will be tagged to it until the next build.

### `envoyproxy/envoy:google-vrp-<version>`

These images contain tools for testing and researching vulnerabilities as part of the [Google
Vulnerability Reward Program (VRP)](https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/security/google_vrp.html)
