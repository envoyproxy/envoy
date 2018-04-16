.. _install_ambassador:

Envoy as an API Gateway in Kubernetes
=====================================

A common scenario for using Envoy is deploying it as an edge service (API
Gateway) in Kubernetes. `Ambassador <https://www.getambassador.io>`_ is an open
source distribution of Envoy designed for Kubernetes. Ambassador uses Envoy for
all L4/L7 management and Kubernetes for reliability, availability, and
scalability. Ambassador operates as a specialized control plane to expose
Envoy's functionality as Kubernetes annotations.

This example will walk through how you can deploy Envoy on Kubernetes via
Ambassador.

Deploying Ambassador
--------------------

Ambassador is configured via Kubernetes deployments. To install Ambassador/Envoy
on Kubernetes, run the following if you're using a cluster with RBAC enabled:

.. code-block:: console

   kubectl apply -f https://www.getambassador.io/yaml/ambassador/ambassador-rbac.yaml

or this if you are not using RBAC:

.. code-block:: console

   kubectl apply -f https://www.getambassador.io/yaml/ambassador/ambassador-no-rbac.yaml

The above YAML will create a Kubernetes deployment for Ambassador that includes
readiness and liveness checks. By default, it will also create 3 instances of
Ambassador. Each Ambassador instance consists of an Envoy proxy along with the
Ambassador control plane.

We'll now need to create a Kubernetes service to point to the Ambassador
deployment. In this example, we'll use a ``LoadBalancer`` service. If your
cluster doesn't support ``LoadBalancer`` services, you'll need to change to a
``NodePort`` or ``ClusterIP``.

.. code-block:: yaml

  ---
  apiVersion: v1
  kind: Service
  metadata:
    labels:
      service: ambassador
    name: ambassador
  spec:
    type: LoadBalancer
    ports:
    - port: 80
      targetPort: 80
    selector:
      service: ambassador

Save this YAML to a file ``ambassador-svc.yaml``. Then, deploy this service to
Kubernetes:

.. code-block:: console

   kubectl apply -f ambassador-svc.yaml

At this point, Envoy is now running on your cluster, along with the Ambassador
control plane.

Configuring Ambassador
----------------------

Ambassador uses Kubernetes annotations to add or remove configuration. This
sample YAML will add a route to Google, similar to the basic configuration
example in the :ref:`Getting Started guide <start>`.

.. code-block:: yaml

  ---
  apiVersion: v1
  kind: Service
  metadata:
    name: google
    annotations:
      getambassador.io/config: |
        ---
        apiVersion: ambassador/v0
        kind:  Mapping
        name:  google_mapping
        prefix: /google/
        service: https://google.com:443
        host_rewrite: www.google.com
  spec:
    type: ClusterIP
    clusterIP: None

Save the above into a file called ``google.yaml``. Then run:

.. code-block:: console

   kubectl apply -f google.yaml

Ambassador will detect the change to your Kubernetes annotation and add the
route to Envoy. Note that we used a dummy service in this example; typically,
you would associate the annotation with your real Kubernetes service.

Testing the mapping
-------------------

You can test this mapping by getting the external IP address for the Ambassador
service, and then sending a request via ``curl``.

.. code-block:: console

   $ kubectl get svc ambassador
   NAME         CLUSTER-IP     EXTERNAL-IP     PORT(S)        AGE
   ambassador   10.19.241.98   35.225.154.81   80:32491/TCP   15m
   $ curl -v 35.225.154.81/google/

More
----

Ambassador exposes multiple Envoy features on mappings, such as CORS, weighted
round robin, gRPC, TLS, and timeouts. For more information, read the
`configuration documentation
<https://www.getambassador.io/reference/configuration>`_.
