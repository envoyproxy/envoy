Fixed a crash when a dynamic module committed an event through a scheduler from one of its own threads
while Envoy released the last other reference to the scheduler's owner, for example during a listener
update. The owner's destructor and the module's destroy hook could then run on the module's thread and
cancel callouts or reset streams that belong to the main or worker thread. The HTTP, network and listener
filter, filter configuration, bootstrap extension, stats sink and TLS certificate selector schedulers no
longer hold a reference to their owner, so a commit can no longer destroy it. Events committed after the
owner starts its teardown are dropped.
