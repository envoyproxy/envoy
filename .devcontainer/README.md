# Envoy Dev Container

This directory contains some experimental tools for Envoy Development in [VSCode Remote - Containers](https://code.visualstudio.com/docs/remote/containers).

## How to use

Open with VSCode with the Container extension installed. Follow the [official guide](https://code.visualstudio.com/docs/remote/containers) to open this 
repository directly from GitHub or from checked-out source tree.

After opening, run the `Refresh Compilation Database` task to generate compilation database to navigate in source code. 
This will run partial build of Envoy and may take a while depends on the machine performance. This task is needed to run everytime after
changing BUILD files to get the changes reflected. 

## Advanced Usages

### Using Remote Build Execution

WIP

### Disk performance

Docekr for Mac/Windows is known to have disk performance issue, this is very obvious when you try to format all files in the container.
[Update the mount consistency to 'delegated'](https://code.visualstudio.com/docs/remote/containers-advanced#_update-the-mount-consistency-to-delegated-for-macos) is recommended.