.. _install_gloo:

Envoy as Ingress, API and Function Gateway for Kubernetes with Gloo
===================================================================

Kubernetes users often need to allow traffic to flow from and to the cluster, 
and Envoy is great for that purpose.
The open source project `Gloo <https://gloo.solo.io>`_, which is built on top 
of Envoy, is designed for microservices, monoliths and also applications that
might want to leverage function as a service. Gloo can decouple client APIs 
from upstream APIs at the routing level. In a simplistic way, Gloo is a 
great and easy to use tool to get traffic inside your Kubernetes cluster.

Continue reading for more information on how to get started with Gloo.
This should only take a few minutes

Installing Gloo
---------------

For this installation, there are three main prerequisites:

* **Kubernetes version**: Gloo requires version 1.8 or higher. `Minikube <https://kubernetes.io/docs/setup/minikube/>`_
  is an easy way to get access to your own local Kubernetes installation.
* **kubectl**: you need access to the `kubectl` command line tool.
* **glooctl**: this is the Gloo command line tool which you will use to interact 
  with the open source version of Gloo. Check the `releases <https://github.com/solo-io/gloo/releases>`_ 
  page under Gloo's project repository to download the latest release. 
  There you will find versions compatible with macOS and Linux.

Once all you have the above, all you need to do is run the following command:

.. code-block:: console

   glooctl install kube 

If you are familiar with Kubernetes, the command above will tell kubernetes what and 
how it should run the Gloo images. The Gloo pods should be running in a namespace called
``gloo-system``.

Your output should look similar to this:

.. code-block:: console

    namespace/gloo-system created
    customresourcedefinition.apiextensions.k8s.io/upstreams.gloo.solo.io created
    customresourcedefinition.apiextensions.k8s.io/virtualservices.gloo.solo.io created
    customresourcedefinition.apiextensions.k8s.io/roles.gloo.solo.io created
    customresourcedefinition.apiextensions.k8s.io/attributes.gloo.solo.io created
    configmap/ingress-config created
    clusterrole.rbac.authorization.k8s.io/gloo-role created
    clusterrole.rbac.authorization.k8s.io/gloo-discovery-role created
    clusterrolebinding.rbac.authorization.k8s.io/gloo-cluster-admin-binding created
    clusterrolebinding.rbac.authorization.k8s.io/gloo-discovery-cluster-admin-binding created
    clusterrole.rbac.authorization.k8s.io/gloo-knative-upstream-discovery-role created
    clusterrolebinding.rbac.authorization.k8s.io/gloo-knative-upstream-discovery-binding created
    deployment.apps/control-plane created
    service/control-plane created
    deployment.apps/function-discovery created
    deployment.apps/ingress created
    service/ingress created
    deployment.extensions/kube-ingress-controller created
    deployment.extensions/upstream-discovery created
    Gloo successfully installed.

Checking your Installation
--------------------------

For more details on what is running in the ``gloo-system`` namespace, run the following
command:

.. code-block:: console

   kubectl get all -n gloo-system

Your output should look similar to this:

.. code-block:: console
   
    NAME                                           READY   STATUS    RESTARTS   AGE
    pod/control-plane-6fc6dc7545-xrllk             1/1     Running   0          11m
    pod/function-discovery-544c596dcd-gk8x7        1/1     Running   0          11m
    pod/ingress-64f75ccb7-4z299                    1/1     Running   0          11m
    pod/kube-ingress-controller-665d59bc7d-t6lwk   1/1     Running   0          11m
    pod/upstream-discovery-74db4d7475-gqrst        1/1     Running   0          11m

    NAME                    TYPE           CLUSTER-IP       EXTERNAL-IP   PORT(S)                         AGE
    service/control-plane   ClusterIP      10.101.206.34    <none>        8081/TCP                        11m
    service/ingress         LoadBalancer   10.108.115.187   <pending>     8080:32608/TCP,8443:30634/TCP   11m

    NAME                                      DESIRED   CURRENT   UP-TO-DATE   AVAILABLE   AGE
    deployment.apps/control-plane             1         1         1            1           11m
    deployment.apps/function-discovery        1         1         1            1           11m
    deployment.apps/ingress                   1         1         1            1           11m
    deployment.apps/kube-ingress-controller   1         1         1            1           11m
    deployment.apps/upstream-discovery        1         1         1            1           11m

    NAME                                                 DESIRED   CURRENT   READY   AGE
    replicaset.apps/control-plane-6fc6dc7545             1         1         1       11m
    replicaset.apps/function-discovery-544c596dcd        1         1         1       11m
    replicaset.apps/ingress-64f75ccb7                    1         1         1       11m
    replicaset.apps/kube-ingress-controller-665d59bc7d   1         1         1       11m
    replicaset.apps/upstream-discovery-74db4d7475        1         1         1       11m


In case your pods are not in ``Running`` state, feel free to jump on the Gloo `slack channel 
<https://slack.solo.io/>`_. 
The community will be able to assist you there.

What's next?
------------

For examples and more documentation on how to use the open source project Gloo, 
check the `project page <https://gloo.solo.io/>`_. 