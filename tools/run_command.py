import subprocess
import time


# Echoes and runs an OS command, returning exit status and the captured
# stdout and stderr as a string array.
def run_command(command):
    start = time.time()
    proc = subprocess.run([command], shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    print(f"COMMAND ({command}) took: {time.time() - start} seconds")

    return proc.returncode, proc.stdout.decode('utf-8').split('\n'), proc.stderr.decode(
        'utf-8').split('\n')
