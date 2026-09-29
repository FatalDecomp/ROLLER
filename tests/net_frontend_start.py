"""Real-UDP listen host/client loading transition (no renderer/window required)."""
from pathlib import Path
import socket
import subprocess
import sys
import time


def ports():
    # Reserve distinct ports together; release just before launching the pair.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as a, \
            socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as b:
        a.bind(("127.0.0.1", 0))
        b.bind(("127.0.0.1", 0))
        return a.getsockname()[1], b.getsockname()[1]


def run_case(exe, track, assets, host_car, client_car, local_players,
             competitors, seed, slow_host):
    host_port, client_port = ports()
    processes = []
    outputs = []
    try:
        for role, port, car, delay in (
                ("host", host_port, host_car, 200 if slow_host else 0),
                ("client", client_port, client_car, 0 if slow_host else 200)):
            command = [exe, track, assets, role, str(port), str(host_port),
                       str(car), str(local_players), str(competitors),
                       str(seed), str(delay)]
            processes.append(subprocess.Popen(
                command, cwd=assets, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
            if role == "host":
                time.sleep(0.15)
        deadline = time.monotonic() + 25
        while any(p.poll() is None for p in processes):
            if any(p.poll() not in (None, 0) for p in processes):
                raise RuntimeError("frontend startup process failed")
            if time.monotonic() > deadline:
                raise TimeoutError("frontend startup timed out")
            time.sleep(0.02)
        if any(p.returncode != 0 for p in processes):
            raise RuntimeError("frontend startup process failed")
        outputs = [p.communicate(timeout=5) for p in processes]
        for out, err in outputs:
            print(out.strip())
            if err:
                print(err, file=sys.stderr)
    except BaseException:
        for p in processes:
            if p.poll() is None:
                p.kill()
        for p in processes:
            out, err = p.communicate(timeout=5)
            print(out, err, file=sys.stderr)
        raise


def main():
    exe, track, assets = (str(Path(p).resolve()) for p in sys.argv[1:])
    browser_port, _ = ports()
    subprocess.run([exe, "--browser", str(browser_port)], check=True, timeout=10)
    for case in range(6):
        client_port, _ = ports()
        subprocess.run([exe, "--internet-join", str(client_port), str(case), track],
                       check=True, timeout=25)
    cases = [
        (3, 1, 1, 8, 12345, False),
        (7, 4, 1, 8, 67890, True),
        (1, 6, 1, 16, 54321, False),
        (6, 2, 1, 16, 13579, True),
        (4, 1, 2, 8, 98765, False),
        (2, 6, 2, 16, 24680, True),
        (0, 3, 2, 16, 19283, False),
    ]
    for case in cases:
        run_case(exe, track, assets, *case)
    print(f"Frontend startup passed: {len(cases)} host/client pairs, "
          "8/16 competitors, both loading orders, single/split-screen cameras")


if __name__ == "__main__":
    main()
