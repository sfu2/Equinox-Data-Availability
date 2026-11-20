#!/usr/bin/env python3
import argparse
import datetime
import json
import os
import pathlib
import re
import subprocess
import sys


WORKFLOW_DIR = pathlib.Path(__file__).resolve().parent
EQUINOX_DIR = WORKFLOW_DIR.parent
REFCNT_DIR = EQUINOX_DIR / "src"
BIN_DIR = REFCNT_DIR / "build-Release" / "bin"
RUNS_DIR = WORKFLOW_DIR / "runs"
GRAPHS_DIR = EQUINOX_DIR / "graphs"

SUMMARY_COLUMNS = [
    "subject",
    "tool",
    "id",
    "cs",
    "both",
    "total",
    "cs_ratio",
    "id_ratio",
    "both_ratio",
]

RATIO_TOKEN = r"[-+]?(?:[0-9.]+|nan|inf)"


class WorkflowError(RuntimeError):
    pass


def parse_ml_result(text):
    counts = re.search(r"id reach:\s*(\d+)\s+cs reach:\s*(\d+)\s+both:\s*(\d+)", text)
    totals = re.search(
        rf"total:\s*(\d+)\s+\[cs:\s*({RATIO_TOKEN})\]\s+\[id:\s*({RATIO_TOKEN})\]\s+\[both / total:\s*({RATIO_TOKEN})\]",
        text,
        re.IGNORECASE,
    )
    if not counts or not totals:
        raise WorkflowError("failed to parse ml_ablation_rc output")
    return {
        "tool": "ml_ablation_rc",
        "id": int(counts.group(1)),
        "cs": int(counts.group(2)),
        "both": int(counts.group(3)),
        "total": int(totals.group(1)),
        "cs_ratio": totals.group(2),
        "id_ratio": totals.group(3),
        "both_ratio": totals.group(4),
    }


def parse_df_result(text):
    counts = re.search(r"id:\s*(\d+)\s+cs:\s*(\d+)\s+both:\s*(\d+)", text)
    totals = re.search(
        rf"total:\s*(\d+)\s+\[cs:\s*({RATIO_TOKEN})\]\s+\[id:\s*({RATIO_TOKEN})\]\s+\[both / total:\s*({RATIO_TOKEN})\]",
        text,
        re.IGNORECASE,
    )
    if not counts or not totals:
        raise WorkflowError("failed to parse df_ablation_rc output")
    return {
        "tool": "df_ablation_rc",
        "id": int(counts.group(1)),
        "cs": int(counts.group(2)),
        "both": int(counts.group(3)),
        "total": int(totals.group(1)),
        "cs_ratio": totals.group(2),
        "id_ratio": totals.group(3),
        "both_ratio": totals.group(4),
    }


def render_summary_tsv(rows):
    lines = ["\t".join(SUMMARY_COLUMNS)]
    for row in rows:
        lines.append("\t".join(str(row.get(column, "")) for column in SUMMARY_COLUMNS))
    return "\n".join(lines) + "\n"


def subject_from_input(input_dir):
    return pathlib.Path(input_dir).resolve().name


def make_run_dir(subject, runs_dir):
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir = runs_dir / f"{subject}-{stamp}"
    counter = 1
    while run_dir.exists():
        run_dir = runs_dir / f"{subject}-{stamp}-{counter}"
        counter += 1
    run_dir.mkdir(parents=True, exist_ok=False)
    return run_dir


def discover_subject_inputs(graphs_dir):
    graphs_path = pathlib.Path(graphs_dir)
    if not graphs_path.is_dir():
        raise WorkflowError(f"graphs directory does not exist: {graphs_path}")
    subjects = []
    for path in sorted(graphs_path.iterdir(), key=lambda item: item.name):
        if not path.is_dir() or path.name.endswith("_nps"):
            continue
        if path.with_name(path.name + "_nps").is_dir():
            subjects.append((path.name, path))
    if not subjects:
        raise WorkflowError(f"no subject directories with matching _nps siblings under {graphs_path}")
    return subjects


def require_binary(name):
    binary = BIN_DIR / name
    if not binary.is_file():
        raise WorkflowError(f"missing binary: {binary}")
    return binary


def run_tool(name, input_dir, run_dir):
    binary = require_binary(name)
    env = os.environ.copy()
    env["input"] = str(pathlib.Path(input_dir).resolve())
    stdout_path = run_dir / f"{name}.stdout.log"
    stderr_path = run_dir / f"{name}.stderr.log"
    with stdout_path.open("w") as stdout, stderr_path.open("w") as stderr:
        result = subprocess.run(
            [str(binary)],
            cwd=str(run_dir),
            env=env,
            stdout=stdout,
            stderr=stderr,
            text=True,
        )
    if result.returncode != 0:
        raise WorkflowError(
            f"{name} failed with exit code {result.returncode}; see {stdout_path} and {stderr_path}"
        )
    return stdout_path.read_text()


def run_subject(input_dir, run_dir):
    input_path = pathlib.Path(input_dir).resolve()
    nps_path = input_path.with_name(input_path.name + "_nps")
    if not input_path.is_dir():
        raise WorkflowError(f"input directory does not exist: {input_path}")
    if not nps_path.is_dir():
        raise WorkflowError(f"matching _nps input directory does not exist: {nps_path}")

    run_dir.mkdir(parents=True, exist_ok=False)
    query_output = run_tool("query_gen", input_path, run_dir)
    ml_output = run_tool("ml_ablation_rc", input_path, run_dir)
    df_output = run_tool("df_ablation_rc", input_path, run_dir)

    rows = [parse_ml_result(ml_output), parse_df_result(df_output)]
    subject = subject_from_input(input_path)
    for row in rows:
        row["subject"] = subject
    summary_tsv = render_summary_tsv(rows)
    (run_dir / "summary.tsv").write_text(summary_tsv)
    (run_dir / "summary.json").write_text(json.dumps(rows, indent=2) + "\n")
    (run_dir / "query_gen.parsed.txt").write_text(query_output)
    return rows


def run_workflow(input_dir, runs_dir):
    run_dir = make_run_dir(subject_from_input(input_dir), pathlib.Path(runs_dir))
    rows = run_subject(input_dir, run_dir)
    return run_dir, render_summary_tsv(rows)


def run_all_subjects(graphs_dir, runs_dir):
    aggregate_dir = make_run_dir("all-subjects", pathlib.Path(runs_dir))
    rows = []
    for subject, input_dir in discover_subject_inputs(graphs_dir):
        subject_dir = aggregate_dir / subject
        rows.extend(run_subject(input_dir, subject_dir))
    summary_tsv = render_summary_tsv(rows)
    (aggregate_dir / "summary.tsv").write_text(summary_tsv)
    (aggregate_dir / "summary.json").write_text(json.dumps(rows, indent=2) + "\n")
    return aggregate_dir, summary_tsv


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Run ref-cnt query_gen, ml_ablation_rc, and df_ablation_rc in an isolated output directory."
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--input",
        help="Input JSON directory, for example artifacts/bitcodes/smb.3.extracted-json/fs.",
    )
    mode.add_argument(
        "--all-subjects",
        action="store_true",
        help="Run every subject directory under --graphs-dir that has a matching _nps sibling.",
    )
    parser.add_argument(
        "--graphs-dir",
        default=str(GRAPHS_DIR),
        help="Graphs root used with --all-subjects.",
    )
    parser.add_argument(
        "--runs-dir",
        default=str(RUNS_DIR),
        help="Directory that will receive per-run output directories.",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        if args.all_subjects or not args.input:
            run_dir, summary = run_all_subjects(args.graphs_dir, args.runs_dir)
        else:
            run_dir, summary = run_workflow(args.input, args.runs_dir)
    except WorkflowError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"output_dir: {run_dir}")
    print(summary, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
