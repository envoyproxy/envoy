# Adding a sandbox example

## Add a `verify.sh` test to your sandbox

Sandboxes are tested as part of the continuous integration process, which expects
each sandbox to have a `verify.sh` script containing tests for the example.

### Basic layout of the `verify.sh` script

At a minimum the `verify.sh` script should include the necessary parts to start
and stop your sandbox.

Given a sandbox with a single `docker` composition, adding the following
to `verify.sh` will test that the sandbox can be started and stopped.

```bash
#!/bin/bash -e

export NAME=example-sandbox

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

# add example tests here...

```

The `$NAME` variable is used for logging when testing the example, and will
often be the same as the directory name.

### Log before running each test

There is a utility function `run_log` that can be used to indicate in test logs what
is being executed and why, for example:

```bash
run_log "Checking foo.txt was created"
ls foo.txt

run_log "Checking bar.txt was created"
ls bar.txt
```

### Add tests reflecting the documented examples

The tests should follow the steps laid out in the documentation.

For example, if the documentation provides a series of `bash` commands to execute, add these in order to `verify.sh`.

You may wish to grep the responses, or check return codes to ensure the commands respond as expected.

Likewise, if the documentation asks the user to browse to a page - for example http://localhost:8000 -
then you should add a test to ensure that the given URL responds as expected.

If an example web page is also expected to make further JavaScript `HTTP` requests in order to function, then add
tests for requests that mimick this interaction.

A number of utility functions have been added to simplify browser testing.

#### Utility functions: `responds_with`

The `responds_with` function can be used to ensure a request to a given URL responds with
expected `HTTP` content.

It follows the form `responds_with <expected_content> <url> [<curl_args>]`

For example, a simple `GET` request:

```bash
responds_with \
    "Hello, world" \
    http://localhost:8000
```

This is a more complicated example that uses an `HTTPS` `POST` request and sends some
additional headers:

```bash
responds_with \
    "Hello, postie" \
    https://localhost:8000/some-endpoint \
    -k \
    -X POST \
    -d 'data=hello,rcpt=service' \
    -H 'Origin: https://example-service.com'
```

#### Utility functions: `responds_with_header`

You can check that a request responds with an expected header as follows:

```bash
responds_with_header \
    "HTTP/1.1 403 Forbidden" \
    "http://localhost:8000/?name=notdown"
```

`responds_with_header` can accept additional curl arguments like `responds_with`

#### Utility functions: `responds_without_header`

You can also check that a request *does not* respond with a given header:

```bash
responds_without_header \
    "X-Secret: treasure" \
    "http://localhost:8000"
```

`responds_without_header` can accept additional curl arguments like `responds_with`

#### Utility functions: `wait_for`

You can wait for some amount of time (specified in seconds) for a command to return `0`.

The following example will wait for 20 seconds for a service ``my-service`` to become healthy.

```bash
wait_for 20 sh -c "docker-compose ps my-service | grep healthy | grep -v unhealthy"
```

### Slow starting `docker` compositions

Unless your example provides a way for ensuring that all containers are healthy by
the time `docker-compose up -d` returns, you may need to add a `DELAY` before running
the steps in your `verify.sh`

For example, to wait 10 seconds after `docker-compose up -d` has been called, set the
following:

```bash
#!/bin/bash -e

export NAME=example-sandbox
export DELAY=10

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

# add example tests here...
```

### Examples with multiple `docker` compositions

For your example to work it may need more than one `docker` composition to be run.

You can set where to find the `docker-compose.yaml` files with the `PATHS` argument.

By default `PATHS=.`, but you can change this to a comma-separated list of paths.

For example a sandbox containing `frontend/docker-compose.yaml` and `backend/docker-compose.yaml`,
might use a `verify.sh` with:

```bash
#!/bin/bash -e

export NAME=example-sandbox
export PATHS=frontend,backend

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

# add example tests here...
```

### Bringing stacks up manually

You may need to bring up the stack manually, in order to run some steps beforehand.

Sourcing `verify-common.sh` will always leave you in the sandbox directory, and from there
you can use the `bring_up_example` function.

For example:

```bash
#!/bin/bash -e

export NAME=example-sandbox
export MANUAL=true

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Creating bar.txt before starting containers"
echo foo > bar.txt

bring_up_example

# add example tests here...
```

If your sandbox has multiple compositions, and uses the `$PATHS` env var described above,
`bring_up_example` will bring all of your compositions up.

### Additional arguments to `docker-compose up -d`

If you need to pass additional arguments to compose you can set the `UPARGS`
env var.

For example, to scale a composition with a service named `http_service`, you
should add the following:

```bash
#!/bin/bash -e

export NAME=example-sandbox
export UPARGS="--scale http_service=2"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

# add example tests here...
```

### Running commands inside `docker` containers

If your example asks the user to run commands inside containers, you can
mimick this using `docker-compose exec -T`. The `-T` flag is necessary as the
tests do not have access to a `tty` in the CI pipeline.

### Note on permissions and configuration

The sandbox tests are run with a `umask` setting of `027` to ensure they will run in environments
where this is the case.

As the Envoy containers run as non-root, it is essential that any configurations required
by the daemon are included in the relevant example `Dockerfile` rather than mounted in
any `docker-compose.yaml` files.

The Docker recipe should also ensure that added configurations are world-readable.

For example, with an added configuration file named `front-envoy.yaml`, you should add
the following in the Docker recipe:

```
RUN chmod go+r /etc/front-envoy.yaml
```

## Sandbox configuration tests

Example configuration files are tested to ensure they are valid and well-formed, and do
not contain deprecated features.

### Exclude configs from example configuration tests

The CI script searches for all files in the examples folders with a `yaml` or `lua` extension.

These files are bundled into a test and the `yaml` files are used to try to start an Envoy server.

If your example includes `yaml` files that are either not Envoy configuration, or for some reason
cannot be tested in this way, you should add the files to the `exclude` list in the `filegroup.srcs`
section of the `examples/BUILD` file.

The `exclude` patterns are evaluated as `globs` in the context of the `examples` folder.


## Verifying your sandbox

Once you have built your sandbox, and added the `verify.sh` script you can run it directly in the
sandbox folder.

For example:

```
cd examples/example-sandbox
./verify.sh

```

You should see the docker composition brought up, your tests run, and the composition brought down again.

The script should exit with `0` for the tests to pass.


## Verifying multiple/all sandboxes

In continuous integration, all of the sandboxes are checked using the `ci/verify-examples.sh`.

This can also be called with a filter argument, which is a `glob` evaluated in the context of the `examples` folder.

For example, to run all sandboxes with names beginning `jaeger`:

```
./ci/verify-examples.sh jaeger*
```

---

**NOTE**

You can use this script locally to test the sandboxes on your platform, but you should be aware that it requires
a lot of resources as it downloads and builds many Docker images, and then runs them in turn.

---

One way to run the tests in an isolated environment is to mount the `envoy` source into a `docker-in-docker` container
or similar, and then run the script from inside that container.
