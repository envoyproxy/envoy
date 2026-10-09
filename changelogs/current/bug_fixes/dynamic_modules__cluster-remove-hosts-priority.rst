dynamic modules: fixed a bug where removing cluster hosts that were added at a priority above zero
did not republish that priority, so worker load balancers kept routing to the removed endpoints.
Hosts added through the cluster host APIs now also record their real priority, which keeps the cross
priority host map consistent when they are removed.
