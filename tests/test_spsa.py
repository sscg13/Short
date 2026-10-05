#!/usr/bin/env python3
"""Check a native TUNE=1 build, optionally against a TUNE=0 release build.

Usage: python tests/test_spsa.py chess_gcc.exe --release chess_release.exe
Both executables should be built from the same source and network.
"""

import argparse
import csv
import io
import re
import subprocess
import sys
import tempfile
from pathlib import Path


# name, default, minimum, maximum, suggested final perturbation size
PARAMETERS = (
    ("RFP_MARGIN", 100, 50, 200, 8),
    ("NMP_MARGIN", 60, 0, 200, 10),
    ("SEE_QUIET_MARGIN", 80, 0, 160, 8),
    ("SEE_CAPTURE_MARGIN", 20, 0, 80, 4),
    ("ASP_DELTA", 50, 10, 200, 10),
    ("LMR_BASE_Q8", 192, 0, 384, 16),
    ("LMR_SCALE_Q8", 128, 64, 192, 6),
)
DEFAULTS = {name: default for name, default, _, _, _ in PARAMETERS}
SPIN = re.compile(r'\boption="([A-Za-z0-9_]+) -spin (-?\d+) (-?\d+) (-?\d+)"')
DONE = re.compile(r"\bdone=1(?:\s|$)")
POSITION = re.compile(
    r"^position\s+(\d+)/(\d+)\s+depth\s+(\d+)\s+score\s+(-?\d+)"
    r"\s+move\s+([0-9A-Fa-f]+)\s+n=\s*(\d+)\b",
    re.MULTILINE,
)
NODES = re.compile(r"^Nodes searched\s*:\s*(\d+)\s*$", re.MULTILINE)
START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
TABLE_SMOKE_FEN = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"
POST = re.compile(r"^(\d+) (-?\d+) \d+ (\d+) \d+ \d+ 0\s+(.+)$", re.MULTILINE)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def run(executable, arguments=(), commands=None, cwd=None, timeout=120):
    result = subprocess.run(
        [str(executable), *arguments],
        input=commands,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=cwd,
        timeout=timeout,
    )
    require(
        result.returncode == 0,
        "{} {} exited {}\nstdout:\n{}\nstderr:\n{}".format(
            executable.name, " ".join(arguments), result.returncode,
            result.stdout, result.stderr,
        ),
    )
    return result


def test_exporter(executable, cwd):
    result = run(executable, ("spsa",), cwd=cwd)
    require(not result.stderr, "SPSA exporter wrote to stderr: " + result.stderr)
    rows = list(csv.reader(io.StringIO(result.stdout), skipinitialspace=True))
    expected = [
        [name, "int", str(default), str(minimum), str(maximum), str(step), "0.002"]
        for name, default, minimum, maximum, step in PARAMETERS
    ]
    require(rows == expected, "Unexpected SPSA CSV (including any banner):\n" + result.stdout)
    # An exporter should exit before protocol logging or loading a network.
    require(not list(Path(cwd).iterdir()), "SPSA export created files in its empty working directory")


def feature_snapshots(output):
    snapshots = []
    pending = []
    for line in output.splitlines():
        if line.startswith("feature "):
            pending.append(line)
            if DONE.search(line):
                snapshots.append(pending)
                pending = []
    require(not pending, "Feature announcements were not terminated with done=1")
    return snapshots


def tuning_options(snapshot):
    options = {}
    for index, line in enumerate(snapshot):
        for match in SPIN.finditer(line):
            name, current, minimum, maximum = match.groups()
            if name in DEFAULTS:
                require(name not in options, "Duplicate tuning option: " + name)
                require(
                    index < len(snapshot) - 1 or match.end() < DONE.search(line).start(),
                    "Tuning option announced after done=1: " + name,
                )
                options[name] = (int(current), int(minimum), int(maximum))
    return options


def test_options(executable, cwd):
    commands = ["xboard"]
    expected = []
    current = dict(DEFAULTS)

    def snapshot(label):
        commands.append("protover 2")
        expected.append((label, dict(current)))

    snapshot("defaults")
    for name, _, minimum, maximum, _ in PARAMETERS:
        mixed_name = "".join(character.lower() if index % 2 else character.upper()
                             for index, character in enumerate(name))
        # OpenBench uses CECP's option command. Exercise both legal endpoints.
        commands.append("option {}={}".format(name.lower(), minimum))
        current[name] = minimum
        snapshot(name + " CECP minimum")
        commands.append("option {}={}".format(mixed_name, maximum))
        current[name] = maximum
        snapshot(name + " CECP maximum")

        commands.append("setoption name {} value {}".format(name.lower(), minimum))
        current[name] = minimum
        snapshot(name + " setoption minimum")
        commands.append("setoption name {} value {}".format(mixed_name, maximum))
        current[name] = maximum
        snapshot(name + " setoption maximum")

        # Keep a non-endpoint value so accepting bad input is easy to detect.
        sentinel = minimum + (maximum - minimum) // 3
        commands.append("setoption name {} value {}".format(mixed_name, sentinel))
        current[name] = sentinel
        snapshot(name + " setoption case handling")
        invalid = (
            "", "garbage", "{}junk".format(sentinel), "{}.0".format(sentinel),
            "--1", "+", "{} 1".format(sentinel), "0x10",
            str(minimum - 1), str(maximum + 1),
            "2147483648", "-2147483649", "9" * 180, "-" + "9" * 180,
            "9" * 600, "0" * 600 + str(maximum + 1),
        )
        for value in invalid:
            commands.append("option {}={}".format(name, value))
            snapshot(name + " rejected CECP value " + repr(value[:32]))
            commands.append("setoption name {} value {}".format(mixed_name, value))
            snapshot(name + " rejected setoption value " + repr(value[:32]))

    commands.append("new")
    snapshot("settings survive new")
    commands.append("setboard " + START_FEN)
    snapshot("settings survive setboard")
    commands.append("quit")
    result = run(executable, ("xboard",), commands="\n".join(commands) + "\n", cwd=cwd)
    actual = feature_snapshots(result.stdout)
    require(len(actual) == len(expected), "Expected {} feature snapshots, received {}".format(len(expected), len(actual)))
    for lines, (label, values) in zip(actual, expected):
        wanted = {
            name: (values[name], minimum, maximum)
            for name, _, minimum, maximum, _ in PARAMETERS
        }
        observed = tuning_options(lines)
        require(observed == wanted, "{}: expected {}, received {}".format(label, wanted, observed))


def test_release_exporter(executable, cwd):
    result = subprocess.run(
        [str(executable), "spsa"], text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd, timeout=30,
    )
    message = (result.stdout + result.stderr).lower()
    require(result.returncode == 1, "Release SPSA exporter must exit 1")
    require("spsa" in message and "disabled" in message, "Release SPSA exporter needs a clear disabled diagnostic")
    result = run(executable, ("xboard",), commands="xboard\nprotover 2\nquit\n", cwd=cwd)
    snapshots = feature_snapshots(result.stdout)
    require(len(snapshots) == 1, "Release build did not finish feature negotiation")
    require(not tuning_options(snapshots[0]), "Release build announced tuning options")


def virtual_search_signature(executable, cwd, change=None):
    commands = [
        "xboard", "option CPU_model=80286", "option CPU_KHz=1000",
        "option VirtualTime=true", "post",
    ]
    if change is not None:
        commands.append("option {}={}".format(*change))
    commands.extend(("setboard " + TABLE_SMOKE_FEN, "go", "quit"))
    result = run(executable, ("xboard",), commands="\n".join(commands) + "\n", cwd=cwd)
    signature = [
        (int(depth), int(score), int(nodes), pv.strip())
        for depth, score, nodes, pv in POST.findall(result.stdout)
    ]
    require(signature and "move " in result.stdout, "Missing completed virtual search:\n" + result.stdout)
    return signature


def test_runtime_tables(executable, cwd):
    # This low-branching endgame reaches the LMR-active depths at 1000 KHz.
    # Compare completed searches, excluding elapsed time, without fixing a
    # strength target or depending on particular node totals.
    default = virtual_search_signature(executable, cwd)
    for change in (
        ("SEE_QUIET_MARGIN", 0), ("SEE_CAPTURE_MARGIN", 80),
        ("LMR_BASE_Q8", 0), ("LMR_SCALE_Q8", 64),
    ):
        changed = virtual_search_signature(executable, cwd, change)
        common = min(len(default), len(changed))
        require(
            any(default[index] != changed[index] for index in range(common)),
            "{}={} did not change any common completed depth; check table refresh".format(*change),
        )


def bench_signature(executable, depth, cwd):
    result = run(executable, ("bench", str(depth)), cwd=cwd, timeout=300)
    positions = POSITION.findall(result.stdout)
    totals = NODES.findall(result.stdout)
    require(positions, "Missing per-position bench output from " + executable.name)
    require(len(totals) == 1, "Missing or repeated bench node total from " + executable.name)
    require(
        len(positions) == int(positions[0][1])
        and all(int(row[2]) == depth for row in positions),
        "Incomplete bench suite from " + executable.name,
    )
    return positions, totals[0]


def test_default_benches(tuning, release, cwd):
    for depth in (1, 4, 6):
        tuned = bench_signature(tuning, depth, cwd)
        fixed = bench_signature(release, depth, cwd)
        require(tuned == fixed, "Tuning defaults differ from release at bench depth {}:\ntuning={}\nrelease={}".format(depth, tuned, fixed))
        print("PASS bench {}: {} nodes, identical scores/moves/counts".format(depth, tuned[1]))


def executable_path(value):
    path = Path(value).resolve()
    if not path.is_file():
        raise argparse.ArgumentTypeError("Executable does not exist: " + str(path))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=executable_path, help="native TUNE=1 executable")
    parser.add_argument("--release", type=executable_path, help="same source/network built with TUNE=0")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="short-spsa-") as cwd:
        test_exporter(args.executable, cwd)
        print("PASS SPSA exporter")
        test_options(args.executable, cwd)
        print("PASS CECP/setoption values, validation, and reset persistence")
        test_runtime_tables(args.executable, cwd)
        print("PASS runtime SEE/LMR table updates")
        if args.release:
            test_release_exporter(args.release, cwd)
            print("PASS release tuning disabled")
            test_default_benches(args.executable, args.release, cwd)
    print("SPSA checks passed")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, subprocess.TimeoutExpired) as error:
        print("FAIL: {}".format(error), file=sys.stderr)
        sys.exit(1)
