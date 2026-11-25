Fixed a bug in scoped RDS where HTTP requests paused on an ``on_demand`` scope were dropped and
left to hang until the stream idle timeout if the scope was updated or removed before its route
configuration arrived. Pending on-demand callbacks are now transferred to the replacing scope helper
on scope updates and invoked on scope removal so paused requests resume and re-resolve the scope.
