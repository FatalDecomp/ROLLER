from pathlib import Path
import subprocess
import sys
import tempfile


def run_pair(exe, assets, track, ports, designs):
    processes = []
    try:
        for index, (role, port) in enumerate(zip(("HOST", "CLIENT"), ports)):
            log = tempfile.TemporaryFile(mode="w+t", encoding="utf-8")
            process = subprocess.Popen(
                [exe, role, str(port), track, assets,
                 str(designs[index]), str(designs[1 - index])], cwd=assets,
                stdout=log, stderr=subprocess.STDOUT, text=True,
            )
            processes.append((role, process, log))
        failures = []
        for role, process, log in processes:
            process.wait(timeout=20)
            log.seek(0)
            output = log.read()
            if process.returncode:
                failures.append(f"{role} exited {process.returncode}:\n{output}")
            else:
                print(output.splitlines()[-1])
        if failures:
            raise AssertionError("\n".join(failures))
    finally:
        for _, process, log in processes:
            if process.poll() is None:
                process.kill()
                process.wait()
            log.close()


if __name__ == "__main__":
    exe, assets, track = (str(Path(arg).resolve()) for arg in sys.argv[1:])
    run_pair(exe, assets, track, (7777, 7790), (0, 1))
    run_pair(exe, assets, track, (7790, 7777), (3, 5))
    run_pair(exe, assets, track, (7777, 7790), (6, 2))
    print("Legacy LAN lobby passed with either instance on the discovery port")
