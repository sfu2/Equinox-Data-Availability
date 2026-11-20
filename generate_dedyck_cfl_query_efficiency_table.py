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

BUILD_TARGETS = ("query_gen", "ml_rc", "ml_wpds", "df_rc", "df_wpds")
RUN_KINDS = ("leak", "double-free")
KIND_CHOICES = RUN_KINDS + ("both",)
SUBJECTS = (
    ("gpu", "gpu"),
    ("fs", "fs"),
    ("net", "net"),
    ("infiniband", "rdma"),
)
TABLE_HEADERS = ("Subsystem", "#Queries", "DeDyck-CFL (s)", "PDS (s)", "Speedup")
SUMMARY_COLUMNS = (
    "subject",
    "display_name",
    "dedyck_total_pairs",
    "dedyck_yes_pairs",
    "dedyck_ms",
    "pds_total_pairs",
    "pds_yes_pairs",
    "pds_ms",
    "speedup",
)


class QueryEfficiencyTableError(RuntimeError):
    pass


def build_command(build_dir=BUILD_DIR):
    return ["cmake", "--build", str(build_dir), "--target", *BUILD_TARGETS]


def parse_nonnegative_int(raw_value, metric_name, tool):
    try:
        value = int(raw_value)
    except ValueError as error:
        raise QueryEfficiencyTableError(
            f"{tool} metric {metric_name} is not numeric: {raw_value!r}"
        ) from error
    if value < 0:
        raise QueryEfficiencyTableError(f"{tool} metric {metric_name} is negative: {raw_value!r}")
    return value


def parse_nonnegative_float(raw_value, metric_name, tool):
    try:
        value = float(raw_value)
    except ValueError as error:
        raise QueryEfficiencyTableError(
            f"{tool} metric {metric_name} is not numeric: {raw_value!r}"
        ) from error
    if not math.isfinite(value):
        raise QueryEfficiencyTableError(f"{tool} metric {metric_name} is not finite: {raw_value!r}")
    if value < 0:
        raise QueryEfficiencyTableError(f"{tool} metric {metric_name} is negative: {raw_value!r}")
    return value


def parse_rc_output(text, tool):
    counts = re.search(r"total:\s*(\S+)\s+cs id reach:\s*(\S+)", text)
    timing = re.search(r"time:\s*(\S+)\s*ms\b", text, re.IGNORECASE)
    if not counts:
        raise QueryEfficiencyTableError(f"failed to parse {tool} total/reachable-pair counts")
    if not timing:
        raise QueryEfficiencyTableError(f"failed to parse {tool} time in milliseconds")
    return {
        "tool": tool,
        "total_pairs": parse_nonnegative_int(counts.group(1), "total_pairs", tool),
        "yes_pairs": parse_nonnegative_int(counts.group(2), "yes_pairs", tool),
        "time_ms": parse_nonnegative_float(timing.group(1), "time_ms", tool),
    }


def parse_ml_rc_output(text):
    return parse_rc_output(text, "ml_rc")


def parse_df_rc_output(text):
    return parse_rc_output(text, "df_rc")


def parse_wpds_output(text, tool, timing_label):
    counts = re.search(r"total_pairs:\s*(\S+)\s*,\s*yes_pairs:\s*(\S+)", text)
    timing = re.search(rf"{re.escape(timing_label)}:\s*(\S+)\s*ms\b", text, re.IGNORECASE)
    if not counts:
        raise QueryEfficiencyTableError(f"failed to parse {tool} total/reachable-pair counts")
    if not timing:
        raise QueryEfficiencyTableError(f"failed to parse {tool} time in milliseconds")
    return {
        "tool": tool,
        "total_pairs": parse_nonnegative_int(counts.group(1), "total_pairs", tool),
        "yes_pairs": parse_nonnegative_int(counts.group(2), "yes_pairs", tool),
        "time_ms": parse_nonnegative_float(timing.group(1), "time_ms", tool),
    }


def parse_ml_wpds_output(text):
    return parse_wpds_output(text, "ml_wpds", "time")


def parse_df_wpds_output(text):
    return parse_wpds_output(text, "df_wpds", "Queries in")


KIND_CONFIGS = {
    "leak": {
        "run_dir_name": "leak",
        "input_suffix": "",
        "required_input_suffixes": ("", "_nps"),
        "query_prefix": "ml",
        "dedyck_tool": "ml_rc",
        "pds_tool": "ml_wpds",
        "parse_dedyck": parse_ml_rc_output,
        "parse_pds": parse_ml_wpds_output,
        "table_filename": "leak_dedyck_cfl_query_efficiency_table.md",
        "summary_filename": "leak_query_efficiency_summary.tsv",
    },
    "double-free": {
        "run_dir_name": "double-free",
        "input_suffix": "",
        "required_input_suffixes": ("", "_nps"),
        "query_prefix": "df",
        "dedyck_tool": "df_rc",
        "pds_tool": "df_wpds",
        "parse_dedyck": parse_df_rc_output,
        "parse_pds": parse_df_wpds_output,
        "table_filename": "double_free_dedyck_cfl_query_efficiency_table.md",
        "summary_filename": "double_free_query_efficiency_summary.tsv",
    },
}


def selected_kinds(kind):
    if kind == "both":
        return RUN_KINDS
    if kind in RUN_KINDS:
        return (kind,)
    raise QueryEfficiencyTableError(f"unknown table kind: {kind}")


def kind_config(kind):
    try:
        return KIND_CONFIGS[kind]
    except KeyError as error:
        raise QueryEfficiencyTableError(f"unknown table kind: {kind}") from error


def output_filenames_for_kind(kind):
    config = kind_config(kind)
    return config["table_filename"], config["summary_filename"]


def output_paths_for_kind(kind, run_dir):
    table_filename, summary_filename = output_filenames_for_kind(kind)
    run_dir = pathlib.Path(run_dir)
    return run_dir / table_filename, run_dir / summary_filename


def display_name_for_subject(subject):
    for subject_name, display_name in SUBJECTS:
        if subject == subject_name:
            return display_name
    raise QueryEfficiencyTableError(f"unknown subject: {subject}")


def milliseconds_to_seconds_text(milliseconds):
    return f"{milliseconds / 1000.0:.2f}"


def speedup_value(dedyck_ms, pds_ms):
    if dedyck_ms <= 0:
        raise QueryEfficiencyTableError(f"cannot calculate speedup from nonpositive DeDyck-CFL time: {dedyck_ms}")
    return pds_ms / dedyck_ms


def speedup_text(dedyck_ms, pds_ms):
    return f"{speedup_value(dedyck_ms, pds_ms):.2f}x"


def checked_summary_row(subject, dedyck_metrics, pds_metrics):
    if dedyck_metrics["total_pairs"] != pds_metrics["total_pairs"]:
        raise QueryEfficiencyTableError(
            f"{subject}: DeDyck-CFL total_pairs ({dedyck_metrics['total_pairs']}) "
            f"does not match PDS total_pairs ({pds_metrics['total_pairs']})"
        )
    if dedyck_metrics["yes_pairs"] != pds_metrics["yes_pairs"]:
        raise QueryEfficiencyTableError(
            f"{subject}: DeDyck-CFL yes_pairs ({dedyck_metrics['yes_pairs']}) "
            f"does not match PDS yes_pairs ({pds_metrics['yes_pairs']})"
        )

    dedyck_ms = dedyck_metrics["time_ms"]
    pds_ms = pds_metrics["time_ms"]
    return {
        "subject": subject,
        "display_name": display_name_for_subject(subject),
        "dedyck_total_pairs": dedyck_metrics["total_pairs"],
        "dedyck_yes_pairs": dedyck_metrics["yes_pairs"],
        "dedyck_ms": dedyck_ms,
        "pds_total_pairs": pds_metrics["total_pairs"],
        "pds_yes_pairs": pds_metrics["yes_pairs"],
        "pds_ms": pds_ms,
        "speedup": speedup_value(dedyck_ms, pds_ms),
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
        f"{summary_row['dedyck_total_pairs']:,}",
        milliseconds_to_seconds_text(summary_row["dedyck_ms"]),
        milliseconds_to_seconds_text(summary_row["pds_ms"]),
        f"{summary_row['speedup']:.2f}x",
    )


def generate_markdown_table(summary_rows):
    rows_by_subject = {row["subject"]: row for row in summary_rows}
    rows = []
    for subject, _display_name in SUBJECTS:
        if subject not in rows_by_subject:
            raise QueryEfficiencyTableError(f"missing summary row for subject: {subject}")
        rows.append(table_row(rows_by_subject[subject]))
    return render_markdown_table(TABLE_HEADERS, rows, numeric_columns=set(range(1, len(TABLE_HEADERS))))


def render_summary_tsv(summary_rows):
    buffer = io.StringIO()
    writer = csv.DictWriter(buffer, fieldnames=SUMMARY_COLUMNS, delimiter="\t", lineterminator="\n")
    writer.writeheader()
    for row in summary_rows:
        writer.writerow({column: row.get(column, "") for column in SUMMARY_COLUMNS})
    return buffer.getvalue()


def make_run_dir(runs_dir=RUNS_DIR):
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    base_name = f"dedyck-cfl-query-efficiency-{stamp}"
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
        raise QueryEfficiencyTableError(
            f"{label} failed with exit code {result.returncode}: {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )
    return result


def rebuild_tools(build_dir=BUILD_DIR):
    build_dir = pathlib.Path(build_dir)
    if not build_dir.is_dir():
        raise QueryEfficiencyTableError(f"ref-cnt build directory is missing: {build_dir}")
    run_command(build_command(build_dir), ROOT_DIR, "CMake build")


def require_binary(name, bin_dir=BIN_DIR):
    binary = pathlib.Path(bin_dir) / name
    if not binary.is_file():
        raise QueryEfficiencyTableError(f"missing binary: {binary}")
    return binary


def subject_input_dir(kind, subject, graphs_dir=GRAPHS_DIR):
    input_name = subject + kind_config(kind)["input_suffix"]
    return pathlib.Path(graphs_dir) / input_name


def validate_subject_input(kind, subject, graphs_dir=GRAPHS_DIR):
    input_dir = subject_input_dir(kind, subject, graphs_dir)
    for suffix in kind_config(kind)["required_input_suffixes"]:
        required_dir = pathlib.Path(graphs_dir) / f"{subject}{suffix}"
        if not required_dir.is_dir():
            raise QueryEfficiencyTableError(f"{kind} input directory is missing: {required_dir}")
    return input_dir.resolve()


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
        raise QueryEfficiencyTableError(
            f"{name} failed with exit code {result.returncode}; see {stdout_path} and {stderr_path}"
        )
    return stdout_path.read_text()


def query_pairs_filename(kind, input_dir):
    return f"{kind_config(kind)['query_prefix']}_{pathlib.Path(input_dir).name}_query_pairs.txt"


def require_query_pairs(kind, input_dir, subject_run_dir):
    query_pairs_path = pathlib.Path(subject_run_dir) / query_pairs_filename(kind, input_dir)
    if not query_pairs_path.is_file():
        raise QueryEfficiencyTableError(f"query_gen did not produce {query_pairs_path.name} in {subject_run_dir}")
    return query_pairs_path


def run_subject(kind, subject, run_dir, graphs_dir=GRAPHS_DIR, bin_dir=BIN_DIR):
    config = kind_config(kind)
    input_dir = validate_subject_input(kind, subject, graphs_dir)
    subject_run_dir = pathlib.Path(run_dir) / config["run_dir_name"] / subject
    subject_run_dir.mkdir(parents=True, exist_ok=False)

    run_tool("query_gen", input_dir, subject_run_dir, bin_dir)
    require_query_pairs(kind, input_dir, subject_run_dir)
    dedyck_output = run_tool(config["dedyck_tool"], input_dir, subject_run_dir, bin_dir)
    pds_output = run_tool(config["pds_tool"], input_dir, subject_run_dir, bin_dir)
    return checked_summary_row(
        subject,
        config["parse_dedyck"](dedyck_output),
        config["parse_pds"](pds_output),
    )


def run_workflow(kind="both", runs_dir=RUNS_DIR, graphs_dir=GRAPHS_DIR, bin_dir=BIN_DIR):
    run_dir = make_run_dir(runs_dir)
    outputs = {}
    for run_kind in selected_kinds(kind):
        summary_rows = []
        for subject, _display_name in SUBJECTS:
            summary_rows.append(run_subject(run_kind, subject, run_dir, graphs_dir, bin_dir))
        markdown_table = generate_markdown_table(summary_rows)
        summary_tsv = render_summary_tsv(summary_rows)
        table_path, summary_path = output_paths_for_kind(run_kind, run_dir)
        table_path.write_text(markdown_table)
        summary_path.write_text(summary_tsv)
        outputs[run_kind] = {
            "markdown_table": markdown_table,
            "summary_tsv": summary_tsv,
            "table_path": table_path,
            "summary_path": summary_path,
        }
    return run_dir, outputs


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Rebuild ref-cnt query tools and reproduce DeDyck-CFL/PDS query-efficiency tables."
    )
    parser.add_argument(
        "--kind",
        choices=KIND_CHOICES,
        default="both",
        help="table kind to reproduce (default: both)",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        rebuild_tools()
        run_dir, outputs = run_workflow(kind=args.kind)
    except QueryEfficiencyTableError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"output_dir: {run_dir}")
    for run_kind in selected_kinds(args.kind):
        output = outputs[run_kind]
        print(f"{run_kind}_table: {output['table_path']}")
        print(output["markdown_table"], end="")
        print(f"{run_kind}_summary_tsv: {output['summary_path']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
