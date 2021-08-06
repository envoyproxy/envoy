import asyncio
import concurrent.futures
import subprocess
from functools import partial
from typing import AsyncGenerator, Iterable, Optional


class async_subprocess:  # noqa: N801

    @classmethod
    async def parallel(
            cls, commands: Iterable[Iterable[str]],
            **kwargs) -> AsyncGenerator[subprocess.CompletedProcess, Iterable[Iterable[str]]]:
        """Run external subprocesses in parallel

        Yields `subprocess.CompletedProcess` results as they are completed.

        Example usage:

        ```
        import asyncio

        from tools.base.aio import async_subprocess

        async def run_system_commands(commands):
            async for result in async_subprocess.parallel(commands, capture_output=True):
                print(result.returncode)
                print(result.stdout)
                print(result.stderr)

        asyncio.run(run_system_commands(["whoami"] for i in range(0, 5)))
        ```
        """
        # Using a `ProcessPoolExecutor` or `ThreadPoolExecutor` here is somewhat
        # arbitrary as subproc will spawn a new process regardless.
        # Either way - using a custom executor of either type gives considerable speedup,
        # most likely due to the number of workers allocated.
        # In my testing, `ProcessPoolExecutor` gave a very small speedup over a large
        # number of tasks, despite any additional overhead of creating the executor.
        # Without `max_workers` set `ProcessPoolExecutor` defaults to the number of cpus
        # on the machine.
        with concurrent.futures.ProcessPoolExecutor() as pool:
            futures = asyncio.as_completed(
                tuple(
                    asyncio.ensure_future(cls.run(command, executor=pool, **kwargs))
                    for command in commands))
            for result in futures:
                yield await result

    @classmethod
    async def run(
            cls,
            *args,
            loop: Optional[asyncio.AbstractEventLoop] = None,
            executor: Optional[concurrent.futures.Executor] = None,
            **kwargs) -> subprocess.CompletedProcess:
        """This is an asyncio wrapper for `subprocess.run`

        It can be used in a similar way to `subprocess.run` but its non-blocking to
        the main thread.

        Example usage:

        ```
        import asyncio

        from tools.base.aio import async_subprocess

        async def run_system_command():
            result = await async_subprocess.run(["whoami"], capture_output=True)
            print(result.returncode)
            print(result.stdout)
            print(result.stderr)

        asyncio.run(run_system_command())

        ```

        By default it will spawn the process using the main event loop, and that loop's
        default (`ThreadPool`) executor.

        You can provide the loop and/or the executor to change this behaviour.
        """
        loop = loop or asyncio.get_running_loop()
        return await loop.run_in_executor(executor, partial(subprocess.run, *args, **kwargs))
