#!/usr/bin/env python3
import argparse
import os
import re
import sys
from glob import glob
from typing import List, Tuple

# Use a non-interactive backend in case this runs headless
import matplotlib
matplotlib.use("Agg")  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402


def parse_line(line: str) -> Tuple[str, int, float]:
    parts = line.strip().split(",")
    if len(parts) < 3:
        raise ValueError("Expected 3 CSV columns: operation,cycles,microseconds")
    op = parts[0].strip()
    cycles = int(parts[1].strip())
    micros = float(parts[2].strip())
    return op, cycles, micros


def parse_file(path: str, cores: int) -> List[Tuple[int, str, int, float]]:
    rows: List[Tuple[int, str, int, float]] = []
    with open(path, "r", encoding="utf-8") as f:
        for raw in f:
            raw = raw.strip()
            if not raw:
                continue
            try:
                op, cycles, micros = parse_line(raw)
            except Exception:
                # Skip malformed lines but continue
                continue
            rows.append((cores, op, cycles, micros))
    return rows


def collect_rows(out_dir: str) -> List[Tuple[int, str, int, float]]:
    rows: List[Tuple[int, str, int, float]] = []
    pattern = os.path.join(out_dir, "*.txt")
    for path in glob(pattern):
        fname = os.path.basename(path)
        # Expect filenames like "2.txt", "8.txt" → cores = 2, 8, ...
        m = re.match(r"^(\d+)\.txt$", fname)
        if not m:
            continue
        cores = int(m.group(1))
        rows.extend(parse_file(path, cores))
    return rows


def ensure_deps():
    try:
        import pandas as _pd  # noqa: F401
        import seaborn as _sns  # noqa: F401
    except Exception as e:
        print(
            "This script requires pandas and seaborn. Install with: pip install pandas seaborn",
            file=sys.stderr,
        )
        raise e


def plot_distributions(rows: List[Tuple[int, str, int, float]], output_path: str) -> None:
    import pandas as pd
    import seaborn as sns

    if not rows:
        raise RuntimeError("No data found to plot. Make sure out/*.txt files exist and are non-empty.")

    df = pd.DataFrame(rows, columns=["cores", "operation", "cycles", "microseconds"])  # type: ignore

    # Sort categories: cores ascending, operation by a stable order if present
    unique_cores = sorted(df["cores"].unique().tolist())
    # Prefer an intuitive op order if available
    preferred_ops = ["enq", "sched", "yield"]
    ops_present = df["operation"].unique().tolist()
    op_order = [op for op in preferred_ops if op in ops_present]
    for op in ops_present:
        if op not in op_order:
            op_order.append(op)

    df["cores"] = pd.Categorical(df["cores"], categories=unique_cores, ordered=True)
    df["operation"] = pd.Categorical(df["operation"], categories=op_order, ordered=True)

    # Figure size scales with number of core groups
    width_per_group = 0.7
    fig_width = max(8.0, width_per_group * len(unique_cores) * (max(1, len(op_order)) / 2))
    fig_height = 6.0

    plt.figure(figsize=(fig_width, fig_height))
    sns.set_style("whitegrid")

    # Violin plot for distribution shape
    sns.violinplot(
        data=df,
        x="cores",
        y="microseconds",
        hue="operation",
        inner=None,
        cut=0,
        linewidth=0.8,
        dodge=True,
        saturation=0.9,
    )

    # Overlay boxplot for medians/quantiles
    sns.boxplot(
        data=df,
        x="cores",
        y="microseconds",
        hue="operation",
        showcaps=True,
        boxprops={"facecolor": "none", "zorder": 3},
        showfliers=False,
        whiskerprops={"linewidth": 1.0},
        dodge=True,
    )

    # De-duplicate legends (violin + box each add one)
    handles, labels = plt.gca().get_legend_handles_labels()
    if labels:
        # Keep first set corresponding to hue categories
        by_label = {}
        for h, l in zip(handles, labels):
            if l not in by_label:
                by_label[l] = h
        plt.legend(by_label.values(), by_label.keys(), title="operation", bbox_to_anchor=(1.02, 1), loc="upper left")

    plt.xlabel("cores")
    plt.ylabel("microseconds")
    plt.title("Operation time distributions by cores (microseconds)")
    plt.tight_layout()
    plt.savefig(output_path, dpi=200)
    plt.close()


def plot_averages(rows: List[Tuple[int, str, int, float]], output_path: str) -> None:
    import pandas as pd
    import seaborn as sns

    if not rows:
        raise RuntimeError("No data found to plot. Make sure out/*.txt files exist and are non-empty.")

    df = pd.DataFrame(rows, columns=["cores", "operation", "cycles", "microseconds"])  # type: ignore

    # Order categories to match distribution plot
    unique_cores = sorted(df["cores"].unique().tolist())
    preferred_ops = ["enq", "sched", "yield"]
    ops_present = df["operation"].unique().tolist()
    op_order = [op for op in preferred_ops if op in ops_present]
    for op in ops_present:
        if op not in op_order:
            op_order.append(op)

    # Aggregate means
    means = (
        df.groupby(["cores", "operation"], as_index=False)["microseconds"].mean()
    )

    means["cores"] = pd.Categorical(means["cores"], categories=unique_cores, ordered=True)
    means["operation"] = pd.Categorical(means["operation"], categories=op_order, ordered=True)

    width_per_group = 0.7
    fig_width = max(8.0, width_per_group * len(unique_cores) * (max(1, len(op_order)) / 2))
    fig_height = 6.0

    plt.figure(figsize=(fig_width, fig_height))
    sns.set_style("whitegrid")

    # Line plot with dots at means
    sns.lineplot(
        data=means,
        x="cores",
        y="microseconds",
        hue="operation",
        marker="o",
        linewidth=2.0,
        markersize=7,
    )

    handles, labels = plt.gca().get_legend_handles_labels()
    if labels:
        by_label = {}
        for h, l in zip(handles, labels):
            if l not in by_label:
                by_label[l] = h
        plt.legend(by_label.values(), by_label.keys(), title="operation", bbox_to_anchor=(1.02, 1), loc="upper left")

    plt.xlabel("cores")
    plt.ylabel("microseconds")
    plt.title("Operation time averages by cores (microseconds)")
    plt.tight_layout()
    plt.savefig(output_path, dpi=200)
    plt.close()


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot distributions of operation times (microseconds) grouped by cores and operation.")
    parser.add_argument(
        "--out-dir",
        default=os.path.join(os.path.dirname(__file__), "out"),
        help="Directory containing X.txt outputs (default: pthread-sim/out)",
    )
    parser.add_argument(
        "--output",
        default=os.path.join(os.path.dirname(__file__), "operation_us_distributions.png"),
        help="Path to save the plot PNG (default: pthread-sim/operation_us_distributions.png)",
    )
    parser.add_argument(
        "--averages-output",
        default=os.path.join(os.path.dirname(__file__), "operation_us_averages.png"),
        help="Path to save the averages line plot PNG (default: pthread-sim/operation_us_averages.png)",
    )
    args = parser.parse_args()

    ensure_deps()
    rows = collect_rows(args.out_dir)
    plot_distributions(rows, args.output)
    plot_averages(rows, args.averages_output)
    print(f"Saved plot to: {args.output}")
    print(f"Saved plot to: {args.averages_output}")


if __name__ == "__main__":
    main()


