Fixed a use-after-free crash when HDS removed a cluster in the same I/O iteration as a
health-check connection failure. The health-check session now stays alive until that iteration
finishes, and a reset delivered after the cluster is gone is ignored instead of calling
``handleFailure()`` on freed state.
