Dynamic module load failures now emit an error log on the ``dynamic_modules`` logger with the form
``Unable to load dynamic module <module> <reason>``. The log is emitted from the module loader for
every extension type, including extension points that have no factory context. The formatter and
health checker extensions now pass the server factory context when loading a module by name, so a
load failure increments the shared ``dynamic_modules.module_load_error`` counter tagged with the
configured instance name.
