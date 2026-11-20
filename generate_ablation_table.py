#!/usr/bin/env python3
import argparse
import csv
import io
import math
import pathlib
import re
import subprocess
import sys


EQUINOX_DIR = pathlib.Path(__file__).resolve().parent
ROOT_DIR = EQUINOX_DIR
BUILD_DIR = EQUINOX_DIR / "src" / "build-Release"
WORKFLOW_SCRIPT = EQUINOX_DIR / "refcnt-workflow" / "run_refcnt_workflow.py"

BUILD_TARGETS = ("query_gen", "ml_ablation_rc", "df_ablation_rc")
SUBJECTS = (
    ("gpu", "gpu"),
    ("fs", "fs"),
    ("net", "net"),
    ("infiniband", "rdma"),
)
TABLE_HEADERS = ("Subsystem", "RSr_De", "RSr_Cs", "RSr_DeCs", "RSk_De", "RSk_Cs", "RSk_DeCs")
REQUIRED_SUMMARY_COLUMNS = (
    "subject",
    "tool",
    "cs_ratio",
    "id_ratio",
    "both_ratio",
)


class AblationTableError(RuntimeError):
    pass


def build_command(build_dir=BUILD_DIR):
    return ["cmake", "--build", str(build_dir), "--target", *BUILD_TARGETS]


def workflow_command(workflow_script=WORKFLOW_SCRIPT):
    return [sys.executable, str(workflow_script), "--all-subjects"]


def parse_summary_tsv(text):
    reader = csv.DictReader(io.StringIO(text), delimiter="\t")
    if reader.fieldnames is None:
        raise AblationTableError("summary.tsv is empty")
    missing_columns = [column for column in REQUIRED_SUMMARY_COLUMNS if column not in reader.fieldnames]
    if missing_columns:
        raise AblationTableError(f"summary.tsv is missing required columns: {', '.join(missing_columns)}")
    rows = []
    for row_number, row in enumerate(reader, start=2):
        if not any((value or "").strip() for value in row.values()):
            continue
        row["_row_number"] = row_number
        rows.append(row)
    return rows


def extract_output_dir(stdout, base_dir=ROOT_DIR):
    match = re.search(r"^output_dir:\s*(?P<path>.+?)\s*$", stdout, re.MULTILINE)
    if not match:
        raise AblationTableError("could not parse output_dir from workflow stdout")
    output_dir = pathlib.Path(match.group("path"))
    if not output_dir.is_absolute():
        output_dir = pathlib.Path(base_dir) / output_dir
    return output_dir


def load_summary(summary_path):
    summary_path = pathlib.Path(summary_path)
    if not summary_path.is_file():
        raise AblationTableError(f"summary.tsv is missing: {summary_path}")
    return parse_summary_tsv(summary_path.read_text())


def format_percent(row, ratio_column):
    raw_value = row.get(ratio_column, "")
    try:
        ratio = float(raw_value)
    except ValueError as error:
        subject = row.get("subject", "<unknown>")
        tool = row.get("tool", "<unknown>")
        raise AblationTableError(
            f"required ratio is not numeric for {subject}/{tool} column {ratio_column}: {raw_value!r}"
        ) from error
    if not math.isfinite(ratio):
        subject = row.get("subject", "<unknown>")
        tool = row.get("tool", "<unknown>")
        raise AblationTableError(
            f"required ratio is not finite for {subject}/{tool} column {ratio_column}: {raw_value!r}"
        )
    return f"{ratio * 100:.2f}"


def ablation_table_rows(summary_rows):
    rows_by_key = {}
    for row in summary_rows:
        rows_by_key[(row["subject"], row["tool"])] = row

    table_rows = []
    for subject, display_name in SUBJECTS:
        ml_row = rows_by_key.get((subject, "ml_ablation_rc"))
        df_row = rows_by_key.get((subject, "df_ablation_rc"))
        if ml_row is None:
            raise AblationTableError(f"missing required summary row: {subject}/ml_ablation_rc")
        if df_row is None:
            raise AblationTableError(f"missing required summary row: {subject}/df_ablation_rc")
        table_rows.append(
            (
                display_name,
                format_percent(ml_row, "id_ratio"),
                format_percent(ml_row, "cs_ratio"),
                format_percent(ml_row, "both_ratio"),
                format_percent(df_row, "id_ratio"),
                format_percent(df_row, "cs_ratio"),
                format_percent(df_row, "both_ratio"),
            )
        )
    return table_rows


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


def generate_table(summary_rows):
    return render_markdown_table(
        TABLE_HEADERS,
        ablation_table_rows(summary_rows),
        numeric_columns=set(range(1, len(TABLE_HEADERS))),
    )


def run_command(command, cwd, label):
    result = subprocess.run(
        command,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        raise AblationTableError(
            f"{label} failed with exit code {result.returncode}: {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )
    return result


def rebuild_tools(build_dir=BUILD_DIR):
    build_dir = pathlib.Path(build_dir)
    if not build_dir.is_dir():
        raise AblationTableError(f"ref-cnt build directory is missing: {build_dir}")
    run_command(build_command(build_dir), ROOT_DIR, "CMake build")


def rerun_workflow():
    result = run_command(workflow_command(), ROOT_DIR, "ref-cnt workflow")
    return extract_output_dir(result.stdout)


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Rebuild ref-cnt ablation tools, rerun all subjects, and emit the RQ3 Markdown table."
    )
    return parser.parse_args(argv)


def main(argv=None):
    parse_args(sys.argv[1:] if argv is None else argv)
    try:
        rebuild_tools()
        output_dir = rerun_workflow()
        table = generate_table(load_summary(output_dir / "summary.tsv"))
        (output_dir / "ablation_table.md").write_text(table)
    except AblationTableError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(table, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
