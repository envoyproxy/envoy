# Envoy Example

The following example runs through the setup of an envoy cluster.

**Step 1: Install Docker**

Ensure that you have a recent versions of `docker`, `docker-compose` and
`docker-machine` installed.

A simple way to achieve this is via the [Docker Toolbox](https://www.docker.com/products/docker-toolbox).

**Step 2: Docker Machine setup**

First let's create a new machine which will hold the

```shell
$ docker-machine create --driver virtualbox default
$ eval $(docker-machine env default)
```

**Step 3: Create an image with the Envoy binary**

_Note: Hopefully we can eliminate the need for this with binary distributions._

This will generate an image named `envoybin` with an envoy binary located at
`/usr/local/bin/envoy`.

```shell
$ pwd
/src/envoy
$ docker build -f example/Dockerfile-envoybin -t envoybin .
```

This will take some time. Ensure that the image is listed from `docker images`
when finished.

**Step 4: Start all of our containers**

Let's now define `docker-compose.yml` which will glue together all of the pieces.

We'll define two "services" as follows:

* `app`: our actual web application
* `front-envoy`: an envoy instance configured to be a reverse proxy and load balancer
for the web application

```shell
$ pwd
/src/envoy/example
$ docker-compose up --build -d
$ docker-compose ps
        Name                       Command               State                      Ports
-------------------------------------------------------------------------------------------------------------
example_app_1           /bin/sh -c python app.py         Up      80/tcp
example_front-envoy_1   /usr/local/bin/wait-for-it ...   Up      0.0.0.0:8000->80/tcp, 0.0.0.0:8001->8001/tcp
```

```shell
$ curl -v $(docker-machine ip default):8000/
> GET / HTTP/1.1
> Host: 192.168.99.100:8000
> User-Agent: curl/7.43.0
> Accept: */*
>
< HTTP/1.1 200 OK
< content-type: text/html; charset=utf-8
< content-length: 24
< x-envoy-upstream-service-time: 1
< server: envoy
< date: Sun, 21 Aug 2016 19:51:19 GMT
< x-envoy-protocol-version: HTTP/1.1
Hello from behind Envoy!
```

Now let's scale up our application nodes to demonstrate the clustering abilities
of envoy.

```shell
$ docker-compose scale app=3
Creating and starting example_app_2 ... done
Creating and starting example_app_3 ... done

# The running envoy doesn't actually discover the new hosts at this point since
# it hasn't re-resolved the DNS entry. :'(

# Next is probably to spin up a simple discovery instance and use SDS.
```
