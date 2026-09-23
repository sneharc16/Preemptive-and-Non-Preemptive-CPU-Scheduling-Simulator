"""Command-line behaviour: flags, input modes, output formats, errors and exit codes."""
import csv
import io
import json
import os
import subprocess
import tempfile

import pytest

from simrun import stdin_text

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASIC_CSV = os.path.join(ROOT, "examples", "basic.csv")
BASIC = [(1, 0, 5), (2, 1, 3), (3, 2, 8), (4, 3, 6), (5, 4, 4)]

EXIT_INPUT, EXIT_USAGE, EXIT_IO = 1, 2, 3


def run(binary, args, stdin="", cwd=None):
    with tempfile.TemporaryDirectory() as tmp:
        r = subprocess.run([binary, *args], input=stdin, capture_output=True, text=True,
                           cwd=cwd or tmp)
        files = sorted(os.listdir(tmp))
    return r, files


def headings(stdout):
    return [line for line in stdout.splitlines() if line.endswith("=>")]


# ---------------- flags that work ----------------

def test_algo_flag_selects_one_policy(sched_bin):
    """The original parser compared 8 chars of the 7-char '--algo=' and never matched."""
    r, _ = run(sched_bin, ["--algo=fcfs", f"--input={BASIC_CSV}", "--no-csv"])
    assert r.returncode == 0, r.stderr
    assert headings(r.stdout) == ["FCFS (FIFO) Scheduling =>"]
    assert "Turnaround:13.40" in r.stdout


def test_algo_subset_uses_canonical_order(sched_bin):
    r, _ = run(sched_bin, ["--algo=rr,sjf", "--quantum=3", f"--input={BASIC_CSV}", "--no-csv"])
    assert r.returncode == 0, r.stderr
    assert headings(r.stdout) == ["SJF (Non-preemptive) Scheduling =>",
                                  "Round Robin Scheduling (q=3) =>"]


def test_algo_all_and_duplicates(sched_bin):
    r1, _ = run(sched_bin, ["--algo=all", "--format=csv"], stdin_text(BASIC))
    r2, _ = run(sched_bin, ["--algo=srtf,fcfs,rr,sjf,fcfs", "--format=csv"], stdin_text(BASIC))
    assert r1.returncode == r2.returncode == 0
    assert r1.stdout == r2.stdout


def test_file_input_matches_stdin_input(sched_bin):
    a, _ = run(sched_bin, ["--format=json", f"--input={BASIC_CSV}"])
    b, _ = run(sched_bin, ["--format=json"], stdin_text(BASIC))
    assert a.returncode == b.returncode == 0
    assert json.loads(a.stdout) == json.loads(b.stdout)


def test_file_input_text_has_no_prompts(sched_bin):
    r, _ = run(sched_bin, [f"--input={BASIC_CSV}", "--no-csv"])
    assert r.stdout.startswith("FCFS (FIFO) Scheduling =>\n")


def test_stdin_text_keeps_legacy_prompts(sched_bin):
    r, _ = run(sched_bin, ["--no-csv"], stdin_text(BASIC))
    assert r.stdout.startswith("Number of Processes: Enter details for each process on its own "
                               "line: PID Arrival Burst\n\nFCFS (FIFO) Scheduling =>\n")


def test_format_csv(sched_bin):
    r, files = run(sched_bin, ["--format=csv", "--quantum=4"], stdin_text(BASIC))
    assert r.returncode == 0 and r.stderr == ""
    rows = list(csv.DictReader(io.StringIO(r.stdout)))
    assert len(rows) == 4 * len(BASIC)
    assert {row["Algorithm"] for row in rows} == {"FCFS", "SJF", "SRTF", "RoundRobin(q=4)"}
    assert files == []  # machine formats do not write schedule_metrics.csv implicitly


def test_format_json_schema(sched_bin):
    r, files = run(sched_bin, ["--format=json", "--algo=rr", "--quantum=3"], stdin_text(BASIC))
    doc = json.loads(r.stdout)
    assert doc["schema_version"] == 1
    assert doc["workload"] == [{"pid": p, "arrival": a, "burst": b} for p, a, b in BASIC]
    (res,) = doc["results"]
    assert set(res) == {"algorithm", "label", "preemptive", "quantum", "processes", "averages",
                        "makespan", "gantt"}
    assert (res["algorithm"], res["label"], res["preemptive"], res["quantum"]) == (
        "rr", "RoundRobin(q=3)", True, 3)
    assert set(res["processes"][0]) == {"pid", "arrival", "burst", "start", "completion",
                                        "response", "waiting", "turnaround"}
    assert set(res["averages"]) == {"response", "waiting", "turnaround"}
    assert all(set(s) == {"start", "end", "pid"} for s in res["gantt"])
    assert files == []


def test_default_text_mode_writes_csv_file(sched_bin):
    r, files = run(sched_bin, [], stdin_text(BASIC))
    assert r.returncode == 0
    assert files == ["schedule_metrics.csv"]
    assert r.stdout.endswith("CSV written: schedule_metrics.csv\n")


def test_csv_file_flags(sched_bin):
    with tempfile.TemporaryDirectory() as tmp:
        r = subprocess.run([sched_bin, "--format=json", "--csv=m.csv"], input=stdin_text(BASIC),
                           capture_output=True, text=True, cwd=tmp)
        assert r.returncode == 0
        with open(os.path.join(tmp, "m.csv")) as f:
            assert f.readline().startswith("Algorithm,PID,")
    r, files = run(sched_bin, ["--csv=x.csv", "--no-csv"], stdin_text(BASIC))
    assert files == [] and "CSV written" not in r.stdout


def test_no_gantt_and_per_tick(sched_bin):
    r, _ = run(sched_bin, ["--no-gantt", "--no-csv"], stdin_text(BASIC))
    assert "Gantt" not in r.stdout
    r, _ = run(sched_bin, ["--per-tick", "--no-csv", "--algo=fcfs"], stdin_text([(1, 2, 2)]))
    assert "Per-tick timeline — FCFS:\nt=2: P1\nt=3: P1\n\n" in r.stdout


def test_help(sched_bin):
    for flag in ("--help", "-h"):
        r, _ = run(sched_bin, [flag])
        assert r.returncode == 0
        assert r.stdout.startswith("Usage: ")
        assert "Available: fcfs sjf srtf rr" in r.stdout


def test_large_bursts_no_overflow(sched_bin):
    """Two bursts of 2e9 overflowed 32-bit int in the original. (RR uses a large quantum:
    with q=2 the chart would genuinely have ~2e9 alternating segments.)"""
    r, _ = run(sched_bin, ["--format=json", "--quantum=1000000000"],
               stdin_text([(1, 0, 2000000000), (2, 0, 2000000000)]))
    assert r.returncode == 0, r.stderr
    doc = json.loads(r.stdout)
    for res in doc["results"]:
        assert res["makespan"] == 4000000000


# ---------------- errors ----------------

def test_huge_schedule_fails_cleanly(sched_bin):
    """RR q=2 on two 2e9-tick bursts really needs ~2e9 Gantt segments; instead of
    exhausting memory the engine stops at its segment limit with a clear error."""
    r, _ = run(sched_bin, ["--algo=rr", "--format=json"],
               stdin_text([(1, 0, 2000000000), (2, 0, 2000000000)]))
    assert r.returncode == EXIT_INPUT
    assert "rr: schedule needs more than 16777216 Gantt segments" in r.stderr
    assert r.stdout == ""


USAGE_ERRORS = [
    (["--bogus"], "unknown option '--bogus'"),
    (["--algo"], "unknown option '--algo'"),
    (["--algo="], "unknown algorithm ''"),
    (["--algo=fcfs,lottery"], "unknown algorithm 'lottery'"),
    (["--algo=fcfs,"], "unknown algorithm ''"),
    (["--quantum=0"], "--quantum must be an integer > 0 (got '0')"),
    (["--quantum=-2"], "--quantum must be an integer > 0"),
    (["--quantum=abc"], "--quantum must be an integer > 0 (got 'abc')"),
    (["--quantum="], "--quantum must be an integer > 0 (got '')"),
    (["--quantum=2.5"], "--quantum must be an integer > 0"),
    (["--format=xml"], "--format must be text, csv or json (got 'xml')"),
    (["--input="], "--input needs a file name"),
    (["--csv="], "--csv needs a file name"),
]


@pytest.mark.parametrize("args,message", USAGE_ERRORS)
def test_usage_errors(sched_bin, args, message):
    r, files = run(sched_bin, args, stdin_text(BASIC))
    assert r.returncode == EXIT_USAGE
    assert message in r.stderr
    assert r.stdout == "" and files == []


INPUT_ERRORS = [
    ("", "failed to read number of processes: unexpected end of input"),
    ("0\n", "number of processes must be positive (got 0)"),
    ("-1\n", "number of processes must be positive"),
    ("x\n", "'x' is not a 64-bit integer"),
    ("2\n1 0 3\n1 1 2\n", "duplicate PID 1"),
    ("1\n1 -1 3\n", "arrival of P1 must be >= 0 (got -1)"),
    ("1\n1 0 0\n", "burst of P1 must be > 0 (got 0)"),
    ("1\n1 0 -4\n", "burst of P1 must be > 0"),
    ("1\n-5 0 1\n", "PID must be >= 0"),
    ("1\n1 0 abc\n", "burst of process #1: 'abc' is not a 64-bit integer"),
    ("2\n1 0 3\n", "PID of process #2: unexpected end of input"),
    ("2\n1 0 9223372036854775807\n2 0 1\n", "sum of burst times overflows"),
    ("2\n1 9223372036854775000 1000\n2 0 1\n", "latest arrival + total burst"),
]


@pytest.mark.parametrize("stdin,message", INPUT_ERRORS)
def test_stdin_input_errors(sched_bin, stdin, message):
    r, files = run(sched_bin, ["--format=json"], stdin)
    assert r.returncode == EXIT_INPUT
    assert message in r.stderr
    assert r.stdout == "" and files == []


CSV_ERRORS = [
    ("", "input is empty"),
    ("pid,arrival,burst\n", "no processes listed after the header"),
    ("pid,arrival,burst\n1,0,3\n1,2,2\n", "duplicate PID 1"),
    ("pid,arrival,burst\n1,-3,3\n", "line 2: arrival of P1 must be >= 0"),
    ("pid,arrival,burst\n1,0,0\n", "line 2: burst of P1 must be > 0"),
    ("pid,arrival,burst\n1,0,five\n", "line 2: column 'burst' must be a 64-bit integer"),
    ("pid,arrival\n1,0\n", "missing required column 'burst'"),
    ("1,0,5\n", "unknown column '1'"),
]


@pytest.mark.parametrize("content,message", CSV_ERRORS)
def test_csv_input_errors(sched_bin, content, message):
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "w.csv")
        with open(path, "w") as f:
            f.write(content)
        r, _ = run(sched_bin, [f"--input={path}"])
    assert r.returncode == EXIT_INPUT
    assert message in r.stderr
    assert r.stdout == ""


def test_missing_input_file(sched_bin):
    r, _ = run(sched_bin, ["--input=/nonexistent/w.csv"])
    assert r.returncode == EXIT_IO
    assert "cannot open input file '/nonexistent/w.csv'" in r.stderr


def test_unwritable_csv_path(sched_bin):
    r, _ = run(sched_bin, ["--csv=/nonexistent/dir/out.csv", "--format=json"], stdin_text(BASIC))
    assert r.returncode == EXIT_IO
    assert "cannot open '/nonexistent/dir/out.csv' for writing" in r.stderr
    assert r.stdout == ""
