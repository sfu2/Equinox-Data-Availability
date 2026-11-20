#!/usr/bin/env python3
import argparse
import csv
import datetime
import io
import math
import os
import pathlib
import re
import subprocess
import sys


EQUINOX_DIR = pathlib.Path(__file__).resolve().parent
ROOT_DIR = EQUINOX_DIR
BUILD_DIR = EQUINOX_DIR / "src" / "build-Release"
BIN_DIR = BUILD_DIR / "bin"
GRAPHS_DIR = EQUINOX_DIR / "graphs"
RUNS_DIR = EQUINOX_DIR / "refcnt-workflow" / "runs"

BUILD_TARGETS = ("query_gen", "ml_inter_rc", "df_inter_rc")
TABLE_HEADERS = ("Subsystem", "#Checked", "#Verified", "Verified (%)", "Time (s)")
SUMMARY_COLUMNS = (
    "subject",
    "display_name",
    "total_src",
    "unique_src",
    "verified_src",
    "verified_percent",
    "time_ms",
)


class VerificationTableError(RuntimeError):
    pass


def build_command(build_dir=BUILD_DIR):
    return ["cmake", "--build", str(build_dir), "--target", *BUILD_TARGETS]


def display_name_for_subject(subject):
    if subject == "infiniband":
        return "rdma"
    return subject


def discover_subjects(graphs_dir=GRAPHS_DIR):
    graphs_path = pathlib.Path(graphs_dir)
    if not graphs_path.is_dir():
        raise VerificationTableError(f"graphs directory does not exist: {graphs_path}")

    subjects = []
    for path in sorted(graphs_path.iterdir(), key=lambda item: item.name):
        if not path.is_dir() or path.name.endswith("_nps"):
            continue
        nps_path = path.with_name(path.name + "_nps")
        if nps_path.is_dir():
            subjects.append(path.name)

    if not subjects:
        raise VerificationTableError(f"no subject directories with matching _nps siblings under {graphs_path}")
    return subjects


def subject_input_dir(subject, graphs_dir=GRAPHS_DIR):
    return pathlib.Path(graphs_dir) / subject


def validate_subject_input(subject, graphs_dir=GRAPHS_DIR):
    input_dir = subject_input_dir(subject, graphs_dir)
    nps_dir = input_dir.with_name(input_dir.name + "_nps")
    if not input_dir.is_dir():
        raise VerificationTableError(f"selected graph pair is incomplete; missing subject directory: {input_dir}")
    if not nps_dir.is_dir():
        raise VerificationTableError(f"selected graph pair is incomplete; missing _nps directory: {nps_dir}")
    return input_dir.resolve()


def parse_nonnegative_int(raw_value, metric_name, tool):
    try:
        value = int(raw_value)
    except ValueError as error:
        raise VerificationTableError(f"{tool} metric {metric_name} is not numeric: {raw_value!r}") from error
    if value < 0:
        raise VerificationTableError(f"{tool} metric {metric_name} is negative: {raw_value!r}")
    return value


def parse_nonnegative_float(raw_value, metric_name, tool):
    try:
        value = float(raw_value)
    except ValueError as error:
        raise VerificationTableError(f"{tool} metric {metric_name} is not numeric: {raw_value!r}") from error
    if not math.isfinite(value):
        raise VerificationTableError(f"{tool} metric {metric_name} is not finite: {raw_value!r}")
    if value < 0:
        raise VerificationTableError(f"{tool} metric {metric_name} is negative: {raw_value!r}")
    return value


def parse_inter_rc_output(text, tool):
    counts = re.search(
        r"total:\s*(\S+)\s+cs id reach:\s*(\S+)\s+both\s+reach:\s*(\S+)",
        text,
        re.IGNORECASE,
    )
    source_counts = re.search(r"total_src:\s*(\S+)\s+unique_src:\s*(\S+)", text, re.IGNORECASE)
    timing = re.search(r"time:\s*(\S+)\s*ms\b", text, re.IGNORECASE)
    if not counts:
        raise VerificationTableError(f"failed to parse {tool} total/reachable-pair counts")
    if not source_counts:
        raise VerificationTableError(f"failed to parse {tool} total_src/unique_src counts")
    if not timing:
        raise VerificationTableError(f"failed to parse {tool} time in milliseconds")

    metrics = {
        "tool": tool,
        "total_pairs": parse_nonnegative_int(counts.group(1), "total_pairs", tool),
        "id_reach": parse_nonnegative_int(counts.group(2), "id_reach", tool),
        "both_reach": parse_nonnegative_int(counts.group(3), "both_reach", tool),
        "total_src": parse_nonnegative_int(source_counts.group(1), "total_src", tool),
        "unique_src": parse_nonnegative_int(source_counts.group(2), "unique_src", tool),
        "time_ms": parse_nonnegative_float(timing.group(1), "time_ms", tool),
    }
    return checked_metrics(metrics)


def parse_ml_inter_rc_output(text):
    return parse_inter_rc_output(text, "ml_inter_rc")


def parse_df_inter_rc_output(text):
    return parse_inter_rc_output(text, "df_inter_rc")


def checked_metrics(metrics):
    total_src = metrics["total_src"]
    unique_src = metrics["unique_src"]
    tool = metrics["tool"]
    if unique_src > total_src:
        raise VerificationTableError(
            f"{tool} unique_src ({unique_src}) is greater than total_src ({total_src})"
        )
    if total_src == 0:
        raise VerificationTableError(f"{tool} total_src is zero; verified percentage is undefined")
    return metrics


def verification_summary_row(subject, metrics):
    checked_metrics(metrics)
    total_src = metrics["total_src"]
    unique_src = metrics["unique_src"]
    verified_src = total_src - unique_src
    return {
        "subject": subject,
        "display_name": display_name_for_subject(subject),
        "total_src": total_src,
        "unique_src": unique_src,
        "verified_src": verified_src,
        "verified_percent": verified_src / total_src * 100.0,
        "time_ms": metrics["time_ms"],
    }


def render_markdown_table(headers, rows, numeric_columns):
    widths = [len(header) for header in headers]
    for row in rows:
        for index, cell in enumerate(row):
            widths[index] = max(widths[index], len(str(cell)))

    def render_row(row):
        cells = []
        for index, cell in enumerate(row):
            text = str(cell)
            if index in numeric_columns:
                text = text.rjust(widths[index])
            else:
                text = text.ljust(widths[index])
            cells.append(text)
        return "| " + " | ".join(cells) + " |"

    separator_cells = []
    for index, width in enumerate(widths):
        if index in numeric_columns:
            separator_cells.append("-" * (width + 1) + ":")
        else:
            separator_cells.append(":" + "-" * (width + 1))

    lines = [render_row(headers), "|" + "|".join(separator_cells) + "|"]
    lines.extend(render_row(row) for row in rows)
    return "\n".join(lines) + "\n"


def table_row(summary_row):
    return (
        summary_row["display_name"],
        f"{summary_row['total_src']:,}",
        f"{summary_row['verified_src']:,}",
        f"{summary_row['verified_percent']:.2f}",
        f"{summary_row['time_ms'] / 1000.0:.2f}",
    )


def generate_markdown_table(title, summary_rows):
    rows = [table_row(row) for row in summary_rows]
    table = render_markdown_table(TABLE_HEADERS, rows, numeric_columns=set(range(1, len(TABLE_HEADERS))))
    return f"{title}\n{'=' * len(title)}\n\n{table}"


def render_summary_tsv(summary_rows):
    buffer = io.StringIO()
    writer = csv.DictWriter(buffer, fieldnames=SUMMARY_COLUMNS, delimiter="\t", lineterminator="\n")
    writer.writeheader()
    for row in summary_rows:
        output_row = {}
        for column in SUMMARY_COLUMNS:
            value = row.get(column, "")
            if column == "verified_percent" and value != "":
                value = f"{value:.6f}"
            output_row[column] = value
        writer.writerow(output_row)
    return buffer.getvalue()


def make_run_dir(runs_dir=RUNS_DIR):
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    base_name = f"verification-tables-{stamp}"
    run_dir = pathlib.Path(runs_dir) / base_name
    counter = 1
    while run_dir.exists():
        run_dir = pathlib.Path(runs_dir) / f"{base_name}-{counter}"
        counter += 1
    run_dir.mkdir(parents=True, exist_ok=False)
    return run_dir


def run_command(command, cwd, label):
    result = subprocess.run(
        command,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        raise VerificationTableError(
            f"{label} failed with exit code {result.returncode}: {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )
    return result


def rebuild_tools(build_dir=BUILD_DIR):
    build_dir = pathlib.Path(build_dir)
    if not build_dir.is_dir():
        raise VerificationTableError(f"ref-cnt build directory is missing: {build_dir}")
    run_command(build_command(build_dir), ROOT_DIR, "CMake build")


def require_binary(name, bin_dir=BIN_DIR):
    binary = pathlib.Path(bin_dir) / name
    if not binary.is_file():
        raise VerificationTableError(f"missing binary: {binary}")
    return binary


def query_pair_filenames(input_dir):
    subject = pathlib.Path(input_dir).name
    return f"ml_{subject}_query_pairs.txt", f"df_{subject}_query_pairs.txt"


def require_query_pairs(input_dir, subject_run_dir):
    missing = []
    for filename in query_pair_filenames(input_dir):
        path = pathlib.Path(subject_run_dir) / filename
        if not path.is_file():
            missing.append(filename)
    if missing:
        raise VerificationTableError(
            f"query_gen did not produce expected query-pair files in {subject_run_dir}: "
            f"{', '.join(missing)}"
        )


def run_tool(name, input_dir, subject_run_dir, bin_dir=BIN_DIR):
    binary = require_binary(name, bin_dir)
    env = os.environ.copy()
    env["input"] = str(pathlib.Path(input_dir).resolve())
    stdout_path = subject_run_dir / f"{name}.stdout.log"
    stderr_path = subject_run_dir / f"{name}.stderr.log"
    with stdout_path.open("w") as stdout, stderr_path.open("w") as stderr:
        result = subprocess.run(
            [str(binary.resolve())],
            cwd=str(subject_run_dir),
            env=env,
            stdout=stdout,
            stderr=stderr,
            text=True,
        )
    if result.returncode != 0:
        raise VerificationTableError(
            f"{name} failed with exit code {result.returncode}; see {stdout_path} and {stderr_path}"
        )
    return stdout_path.read_text()


RUN_CONFIGS = {
    "equinox": {
        "tool": "ml_inter_rc",
        "parse": parse_ml_inter_rc_output,
        "title": "Result of EQUINOX Verification",
        "table_filename": "result_of_equinox_verification.md",
        "summary_filename": "result_of_equinox_verification.tsv",
    },
    "refcount": {
        "tool": "df_inter_rc",
        "parse": parse_df_inter_rc_output,
        "title": "Result of Refcount Verification",
        "table_filename": "result_of_refcount_verification.md",
        "summary_filename": "result_of_refcount_verification.tsv",
    },
}


def run_subject(kind, subject, run_dir, graphs_dir=GRAPHS_DIR, bin_dir=BIN_DIR):
    config = RUN_CONFIGS[kind]
    input_dir = validate_subject_input(subject, graphs_dir)
    subject_run_dir = pathlib.Path(run_dir) / kind / subject
    subject_run_dir.mkdir(parents=True, exist_ok=False)

    run_tool("query_gen", input_dir, subject_run_dir, bin_dir)
    require_query_pairs(input_dir, subject_run_dir)
    output = run_tool(config["tool"], input_dir, subject_run_dir, bin_dir)
    return verification_summary_row(subject, config["parse"](output))


def write_outputs(kind, run_dir, summary_rows):
    config = RUN_CONFIGS[kind]
    markdown_table = generate_markdown_table(config["title"], summary_rows)
    summary_tsv = render_summary_tsv(summary_rows)
    table_path = pathlib.Path(run_dir) / config["table_filename"]
    summary_path = pathlib.Path(run_dir) / config["summary_filename"]
    table_path.write_text(markdown_table)
    summary_path.write_text(summary_tsv)
    return {
        "markdown_table": markdown_table,
        "summary_tsv": summary_tsv,
        "table_path": table_path,
        "summary_path": summary_path,
    }


def run_workflow(runs_dir=RUNS_DIR, graphs_dir=GRAPHS_DIR, bin_dir=BIN_DIR):
    subjects = discover_subjects(graphs_dir)
    run_dir = make_run_dir(runs_dir)
    outputs = {}
    for kind in ("equinox", "refcount"):
        summary_rows = []
        for subject in subjects:
            summary_rows.append(run_subject(kind, subject, run_dir, graphs_dir, bin_dir))
        outputs[kind] = write_outputs(kind, run_dir, summary_rows)
    return run_dir, outputs


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Rebuild ref-cnt inter-Dyck verification tools and emit source/sink verification tables."
    )
    parser.add_argument(
        "--graphs-dir",
        default=str(GRAPHS_DIR),
        help="Graphs root containing subject and subject_nps directories.",
    )
    parser.add_argument(
        "--runs-dir",
        default=str(RUNS_DIR),
        help="Directory that will receive the verification-tables run directory.",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        rebuild_tools()
        run_dir, outputs = run_workflow(runs_dir=args.runs_dir, graphs_dir=args.graphs_dir)
    except VerificationTableError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"output_dir: {run_dir}")
    for kind in ("equinox", "refcount"):
        output = outputs[kind]
        print(f"{kind}_table: {output['table_path']}")
        print(output["markdown_table"], end="")
        print(f"{kind}_summary_tsv: {output['summary_path']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
