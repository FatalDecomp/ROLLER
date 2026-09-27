"""NET-E8-S4 real-UDP dedicated server race across three processes."""
import argparse
from pathlib import Path
import socket
import subprocess
import time


def free_udp_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def stop(process):
    if process.poll() is None:
        process.kill()
    stdout, stderr = process.communicate(timeout=5)
    return stdout, stderr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--bot", required=True)
    parser.add_argument("--track", required=True)
    parser.add_argument("--assets", required=True)
    args = parser.parse_args()
    for name in ("server", "bot", "track", "assets"):
        setattr(args, name, str(Path(getattr(args, name)).resolve()))

    port = free_udp_port()
    common = ["--port", str(port), "--track-path", args.track,
              "--assets-path", args.assets, "--cars", "2"]
    commands = [
        [args.server, *common, "--players", "2", "--laps", "1",
         "--seed", "12345"],
        [args.bot, *common, "--car", "0", "--name", "BOT0"],
        [args.bot, *common, "--car", "1", "--name", "BOT1"],
    ]
    processes = []
    outputs = []
    start = time.monotonic()
    try:
        processes.append(subprocess.Popen(
            commands[0], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True))
        time.sleep(0.25)
        for command in commands[1:]:
            processes.append(subprocess.Popen(
                command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True))
        for process in processes:
            stdout, stderr = process.communicate(timeout=120)
            outputs.append((stdout, stderr))
        for command, process, (stdout, stderr) in zip(
                commands, processes, outputs):
            assert process.returncode == 0, (command, stdout, stderr)
        assert "Race complete: 2 finishers, 2 human finishers" in outputs[0][0], outputs
        for index in range(2):
            bot_output = outputs[index + 1][0]
            assert f"Bot BOT{index} finished" in bot_output, outputs
            assert f"Bot BOT{index} complete:" in bot_output, outputs
            assert "0 rejected" in bot_output, outputs
    except BaseException:
        for process in processes:
            outputs.append(stop(process))
        print("NET-E8-S4 multi-process race failed")
        for index, (command, output) in enumerate(zip(commands, outputs)):
            print(f"process {index}: {command}\nstdout:\n{output[0]}\nstderr:\n{output[1]}")
        raise
    elapsed = time.monotonic() - start
    print(f"NET-E8-S4 multi-process race passed on UDP port {port} in {elapsed:.2f}s")


if __name__ == "__main__":
    main()
