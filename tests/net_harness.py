"""NET-E0 multi-process stepping and raw UDP acceptance (one world/process)."""
import argparse
import json
from pathlib import Path
import queue
import socket
import subprocess
import threading
import time


class Node:
    def __init__(self, args):
        self.logs = []
        self.process = subprocess.Popen(args, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, text=True)
        self.lines = queue.Queue()
        threading.Thread(target=self._read, args=(self.process.stdout, True), daemon=True).start()
        threading.Thread(target=self._read, args=(self.process.stderr, False), daemon=True).start()
        try:
            self.ready = json.loads(self.lines.get(timeout=10))
            self.socket = socket.create_connection(("127.0.0.1", self.ready["control"]), timeout=5)
            self.reader = self.socket.makefile("r", encoding="ascii")
        except BaseException:
            self.process.kill()
            self.process.wait()
            raise RuntimeError("Node startup failed: " + repr(args) + "\n" + "".join(self.logs))

    def _read(self, stream, responses):
        for line in stream:
            self.logs.append(line)
            if responses:
                self.lines.put(line)

    def command(self, command):
        self.socket.sendall((command + "\n").encode("ascii"))
        result = json.loads(self.reader.readline())
        if "error" in result or result.get("ok") is False:
            raise AssertionError((command, result, self.logs))
        return result

    def close(self):
        try:
            self.command("quit")
            self.process.wait(timeout=3)
            assert self.process.returncode == 0
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait()
            self.reader.close()
            self.socket.close()
            self.process.stdout.close()
            self.process.stderr.close()


def run(args, seed):
    nodes = []
    try:
        for _ in range(2):
            nodes.append(Node([args.server, "--net-harness", "0", "--track-path", args.track,
                               "--assets-path", args.assets]))
        proxy = Node([args.proxy, str(seed)])
        nodes.append(proxy)
        for index, node in enumerate(nodes[:2]):
            proxy.command(f"peer {index} {node.ready['udp']}")
            node.command("step 147")
            context = node.command("context")
            assert context["game_frame"] > 145 and context["race_started"]
        for tick in range(200):
            for node in nodes[:2]:
                node.command(f"send {proxy.ready['udp']} {tick}")
                # Drain exactly this sender before the other can arrive.
                result = proxy.command(f"pump {tick * 28} 1")
                for recipient, count in zip(nodes[:2], result["delivered"]):
                    if count:
                        recipient.command(f"drain {count}")
                node.command("step 1")
        result = proxy.command("pump 6000 0")
        for recipient, count in zip(nodes[:2], result["delivered"]):
            if count:
                recipient.command(f"drain {count}")
        result = []
        for node in nodes[:2]:
            stats = node.command("stats")
            assert stats["tick"] == 347, stats
            assert 175 <= stats["packets"] <= 200, stats
            cars = [node.command(f"car {index}") for index in range(16)]
            assert all(car["speed"] > 0 for car in cars), cars
            result.append((stats, cars, node.command("rng"), node.command("context")))
        return result
    except BaseException:
        print(f"NET harness failure seed={seed}")
        for index, node in enumerate(nodes):
            print(f"process {index}:\n{''.join(node.logs)}")
        raise
    finally:
        for node in reversed(nodes):
            node.close()


def main():
    parser = argparse.ArgumentParser()
    for name in ("server", "proxy", "track", "assets"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    for name in ("server", "proxy", "track", "assets"):
        setattr(args, name, str(Path(getattr(args, name)).resolve()))
    start = time.monotonic()
    first = run(args, 54321)
    assert first == run(args, 54321), "same-seed run diverged"
    elapsed = time.monotonic() - start
    assert elapsed < 30, elapsed
    print(f"NET-E0 harness passed seed=54321, two runs, {elapsed:.2f}s; packets "
          f"{[entry[0]['packets'] for entry in first]}")


if __name__ == "__main__":
    main()
