#!/usr/bin/env python3
"""
Script to analyze pthread-sim output files and plot lock failure statistics.
"""

import os
import re
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

def parse_output_file(file_path):
    """
    Parse a single output file to extract lock failure data.
    
    Args:
        file_path (str): Path to the output file
        
    Returns:
        dict: Dictionary with lock type as key and failure count as value
    """
    lock_failures = {}
    
    try:
        with open(file_path, 'r') as f:
            for line in f:
                # Look for the line starting with "failed glist trylock"
                if line.startswith("failed glist trylock"):
                    # Extract all key/value pairs after the word "trylock"
                    # Example formats observed:
                    #   failed glist trylock getMin 4 minGroup 0 time 0 list 0
                    parts = line.strip().split()

                    # Find index of the header token 'trylock' and parse subsequent pairs
                    try:
                        header_idx = parts.index('trylock')
                    except ValueError:
                        header_idx = 2  # fallback: after "failed glist"

                    # Walk tokens two at a time: <lock_type> <int>
                    i = header_idx + 1
                    while i + 1 < len(parts):
                        key = parts[i]
                        value_token = parts[i + 1]
                        try:
                            value = int(value_token)
                        except ValueError:
                            # If value isn't an int, skip just this token and continue
                            i += 1
                            continue
                        lock_failures[key] = value
                        i += 2

                    break  # Only process the first matching line
    except FileNotFoundError:
        print(f"Warning: File {file_path} not found")
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
    
    return lock_failures

def analyze_output_directory(out_dir):
    """
    Analyze all output files in the specified directory.
    
    Args:
        out_dir (str): Path to the output directory
        
    Returns:
        dict: Dictionary with cores as keys and lock failure data as values
    """
    results = {}
    
    # Get all .txt files in the output directory
    out_path = Path(out_dir)
    if not out_path.exists():
        print(f"Error: Directory {out_dir} does not exist")
        return results
    
    txt_files = list(out_path.glob("*.txt"))
    
    for file_path in txt_files:
        # Extract number of cores from filename (e.g., "2.txt" -> 2)
        filename = file_path.stem
        try:
            cores = int(filename)
        except ValueError:
            print(f"Warning: Could not extract core count from filename {filename}")
            continue
        
        # Parse the file
        lock_failures = parse_output_file(file_path)
        if lock_failures:
            results[cores] = lock_failures
            print(f"Parsed {file_path}: {lock_failures}")
        else:
            print(f"Warning: No lock failure data found in {file_path}")
    
    return results

def create_plot(data, output_file="lock_failures_plot.png"):
    """
    Create and save a plot of lock failure statistics.
    
    Args:
        data (dict): Dictionary with cores as keys and lock failure data as values
        output_file (str): Output filename for the plot
    """
    if not data:
        print("No data to plot")
        return
    
    # Prepare data for plotting
    cores = sorted(data.keys())
    # Dynamically determine all lock types present in the dataset
    lock_types = sorted({lt for core in cores for lt in data[core].keys()})
    
    # Create figure and axis
    fig, ax = plt.subplots(figsize=(12, 8))
    
    # Set up colors for different lock types using matplotlib's prop cycle
    color_cycle = plt.rcParams['axes.prop_cycle'].by_key().get('color', [])
    # Ensure we have enough colors
    if len(color_cycle) < len(lock_types):
        # Extend with a secondary palette if needed
        color_cycle = (color_cycle * ((len(lock_types) // max(1, len(color_cycle))) + 1))[:len(lock_types)]
    colors = {lt: color_cycle[idx] for idx, lt in enumerate(lock_types)}
    
    # Plot data for each lock type
    for lock_type in lock_types:
        failures = []
        for core in cores:
            failures.append(data[core].get(lock_type, 0))
        
        ax.plot(cores, failures, 
                marker='o', 
                linewidth=2, 
                markersize=8,
                color=colors[lock_type],
                label=f'{lock_type} lock',
                alpha=0.8)
    
    # Customize the plot
    ax.set_xlabel('Number of Cores', fontsize=12, fontweight='bold')
    ax.set_ylabel('Number of Failed Lock Attempts', fontsize=12, fontweight='bold')
    ax.set_title('Lock Failure Statistics by Core Count', fontsize=14, fontweight='bold')
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3)
    
    # Set x-axis to show only the core counts we have data for
    ax.set_xticks(cores)
    ax.set_xticklabels([str(core) for core in cores])
    
    # Add some padding to the y-axis
    max_failures = 0
    for core in cores:
        if data[core]:
            max_failures = max(max_failures, max(data[core].values()))
    ax.set_ylim(0, max_failures * 1.1)
    
    # Improve layout
    plt.tight_layout()
    
    # Save the plot
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Plot saved as {output_file}")
    
    # Show the plot
    plt.show()

def parse_operation_metrics_from_line(line):
    """
    Parse a single "cycles:" line to extract per-operation average cycles and microseconds.

    Expected format examples:
        "0 cycles: sched 2157(3.54) enq 166(0.97) yield 785(3.54)"

    Returns:
        dict: op_name -> { 'avg_cycles': float, 'avg_us': float }
    """
    if not re.match(r"^\d+\s+cycles:\s+", line):
        return {}

    line_after_header = re.sub(r"^\d+\s+cycles:\s+", "", line.strip())

    tokens = line_after_header.split()
    op_metrics = {}

    i = 0
    while i < len(tokens):
        op = tokens[i]
        if i + 1 >= len(tokens):
            break
        next_tok = tokens[i + 1]

        avg_cycles = None
        avg_us = None

        # Case 1: "1234(5.67)"
        m = re.match(r"^(\d+)\(([-+]?[0-9]*\.?[0-9]+)\)$", next_tok)
        if m:
            avg_cycles = float(m.group(1))
            avg_us = float(m.group(2))
            i += 2
        else:
            # Case 2: "1234" "(5.67)"
            if i + 2 < len(tokens):
                m_cycles = re.match(r"^(\d+)$", next_tok)
                m_us = re.match(r"^\(([-+]?[0-9]*\.?[0-9]+)\)$", tokens[i + 2])
                if m_cycles and m_us:
                    avg_cycles = float(m_cycles.group(1))
                    avg_us = float(m_us.group(1))
                    i += 3
                else:
                    i += 1
                    continue
            else:
                i += 1
                continue

        op_metrics[op] = { 'avg_cycles': avg_cycles, 'avg_us': avg_us }

    return op_metrics

def analyze_operation_avgs_directory(out_dir):
    """
    Analyze all output files to compute average cycles per operation across cores and files.

    Args:
        out_dir (str): Path to the output directory

    Returns:
        dict: op_name -> average of per-core averages (float)
    """
    out_path = Path(out_dir)
    if not out_path.exists():
        print(f"Error: Directory {out_dir} does not exist")
        return {}

    txt_files = list(out_path.glob("*.txt"))

    # Collect all averages per op across all cores and files (microseconds)
    op_to_us_avgs = {}

    for file_path in txt_files:
        try:
            with open(file_path, 'r') as f:
                for line in f:
                    metrics = parse_operation_metrics_from_line(line)
                    if not metrics:
                        continue
                    for op, vals in metrics.items():
                        op_to_us_avgs.setdefault(op, []).append(vals['avg_us'])
        except Exception as e:
            print(f"Error reading {file_path}: {e}")

    # Compute mean of averages per op (microseconds)
    op_mean_avgs = {op: float(np.mean(vals)) for op, vals in op_to_us_avgs.items() if vals}

    if op_mean_avgs:
        print("\nOperation average microseconds across all cores/files:")
        for op, avg in sorted(op_mean_avgs.items()):
            print(f"  {op}: {avg:.2f} us")

    return op_mean_avgs

def create_operation_avg_plot(op_mean_avgs, output_file="operation_avg_us_plot.png"):
    """
    Create and save a bar chart of average cycles per operation (averaged across cores).

    Args:
        op_mean_avgs (dict): op_name -> average cycles (float)
        output_file (str): Output filename for the plot
    """
    if not op_mean_avgs:
        print("No operation averages to plot")
        return

    ops = sorted(op_mean_avgs.keys())
    values = [op_mean_avgs[op] for op in ops]

    # Colors from the matplotlib color cycle
    color_cycle = plt.rcParams['axes.prop_cycle'].by_key().get('color', [])
    if len(color_cycle) < len(ops):
        color_cycle = (color_cycle * ((len(ops) // max(1, len(color_cycle))) + 1))[:len(ops)]
    colors = [color_cycle[i] for i in range(len(ops))]

    fig, ax = plt.subplots(figsize=(12, 8))
    ax.bar(ops, values, color=colors, alpha=0.85)
    ax.set_xlabel('Operation', fontsize=12, fontweight='bold')
    ax.set_ylabel('Average Microseconds', fontsize=12, fontweight='bold')
    ax.set_title('Average Microseconds per Operation (Averaged Across Cores)', fontsize=14, fontweight='bold')
    ax.grid(True, axis='y', alpha=0.3)
    plt.xticks(rotation=30, ha='right')
    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Plot saved as {output_file}")
    plt.show()

def analyze_operation_avgs_per_file(out_dir):
    """
    Compute per-file (core-count) average microseconds per operation, and also keep cycles for denominator use.

    For each file, we average the per-core averages of each operation,
    resulting in: cores -> { op: mean_avg_cycles }.

    Returns:
        dict[int, dict[str, float]]
    """
    out_path = Path(out_dir)
    if not out_path.exists():
        print(f"Error: Directory {out_dir} does not exist")
        return {}

    txt_files = list(out_path.glob("*.txt"))
    per_file_op_us_avgs = {}
    per_file_op_cycle_avgs = {}

    for file_path in txt_files:
        # Extract number of cores from filename (e.g., "2.txt" -> 2)
        try:
            cores = int(file_path.stem)
        except ValueError:
            print(f"Warning: Could not extract core count from filename {file_path.name}")
            continue

        op_to_us_vals = {}
        op_to_cycle_vals = {}
        try:
            with open(file_path, 'r') as f:
                for line in f:
                    metrics = parse_operation_metrics_from_line(line)
                    if not metrics:
                        continue
                    for op, vals in metrics.items():
                        op_to_us_vals.setdefault(op, []).append(vals['avg_us'])
                        op_to_cycle_vals.setdefault(op, []).append(vals['avg_cycles'])
        except Exception as e:
            print(f"Error reading {file_path}: {e}")
            continue

        # Mean per op for this file
        per_file_op_us_avgs[cores] = {op: float(np.mean(vals)) for op, vals in op_to_us_vals.items() if vals}
        per_file_op_cycle_avgs[cores] = {op: float(np.mean(vals)) for op, vals in op_to_cycle_vals.items() if vals}

    if per_file_op_us_avgs:
        print("\nPer-file operation averages (us):")
        for cores, op_map in sorted(per_file_op_us_avgs.items()):
            ops_str = ", ".join(f"{op}:{avg:.2f}us" for op, avg in sorted(op_map.items()))
            print(f"  {cores}: {ops_str}")

    return per_file_op_us_avgs, per_file_op_cycle_avgs

def create_operation_avg_plot_by_file(per_file_op_avgs, output_file="operation_avg_us_plot.png"):
    """
    Create and save a plot where the x-axis is the file name (core count),
    and each operation is a colored line showing average microseconds per core count.

    Args:
        per_file_op_avgs (dict[int, dict[str, float]]): cores -> { op: avg }
    """
    if not per_file_op_avgs:
        print("No per-file operation averages to plot")
        return

    cores = sorted(per_file_op_avgs.keys())
    # Determine union of all ops
    ops = sorted({op for c in cores for op in per_file_op_avgs[c].keys()})

    # Colors from the matplotlib color cycle
    color_cycle = plt.rcParams['axes.prop_cycle'].by_key().get('color', [])
    if len(color_cycle) < len(ops):
        color_cycle = (color_cycle * ((len(ops) // max(1, len(color_cycle))) + 1))[:len(ops)]
    colors = {op: color_cycle[i] for i, op in enumerate(ops)}

    fig, ax = plt.subplots(figsize=(12, 8))
    for op in ops:
        y_vals = [per_file_op_avgs[c].get(op, 0.0) for c in cores]
        ax.plot(cores, y_vals, marker='o', linewidth=2, markersize=8, color=colors[op], label=op, alpha=0.85)

    ax.set_xlabel('Cores (from filename)', fontsize=12, fontweight='bold')
    ax.set_ylabel('Average Microseconds', fontsize=12, fontweight='bold')
    ax.set_title('Average Microseconds per Operation by Core Count', fontsize=14, fontweight='bold')
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3)
    ax.set_xticks(cores)
    ax.set_xticklabels([str(c) for c in cores])
    # Y-axis padding
    try:
        max_val = max(v for c in cores for v in per_file_op_avgs[c].values())
    except ValueError:
        max_val = 0.0
    ax.set_ylim(0, max_val * 1.1 if max_val > 0 else 1)

    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Plot saved as {output_file}")
    plt.show()

def parse_lock_wait_stats(file_path):
    """
    Parse the lock timing statistics section to extract avg wait cycles for write/read locks.

    Returns:
        dict: { 'write_avg_cycles': float or 0.0, 'read_avg_cycles': float or 0.0 }
    """
    write_avg = 0.0
    read_avg = 0.0
    try:
        with open(file_path, 'r') as f:
            for line in f:
                m_w = re.search(r"Group list write lock:\s*avg\s*(\d+)\s*cycles", line)
                if m_w:
                    write_avg = float(m_w.group(1))
                m_r = re.search(r"Group list read lock:\s*avg\s*(\d+)\s*cycles", line)
                if m_r:
                    read_avg = float(m_r.group(1))
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
    return { 'write_avg_cycles': write_avg, 'read_avg_cycles': read_avg }

def compute_wait_percentages(out_dir):
    """
    For each file (core count), compute the percentage of cycles per operation spent waiting
    for locks, split into read and write, using stacked components.

    We define baseline avg cycles per operation as the mean of per-operation avg cycles
    from the "cycles:" lines in that file.

    Returns:
        dict[int, dict[str, float]]: cores -> { 'read_pct': float, 'write_pct': float }
    """
    out_path = Path(out_dir)
    txt_files = list(out_path.glob("*.txt"))
    percentages = {}

    for file_path in txt_files:
        try:
            cores = int(file_path.stem)
        except ValueError:
            continue

        # Gather avg cycles per op
        avg_cycles_values = []
        try:
            with open(file_path, 'r') as f:
                for line in f:
                    metrics = parse_operation_metrics_from_line(line)
                    if not metrics:
                        continue
                    for vals in metrics.values():
                        avg_cycles_values.append(vals['avg_cycles'])
        except Exception as e:
            print(f"Error reading {file_path}: {e}")
            continue

        baseline_cycles = float(np.mean(avg_cycles_values)) if avg_cycles_values else 0.0

        waits = parse_lock_wait_stats(file_path)
        write_avg = waits['write_avg_cycles']
        read_avg = waits['read_avg_cycles']

        if baseline_cycles <= 0.0:
            read_pct = 0.0
            write_pct = 0.0
        else:
            read_pct = max(0.0, min(1.0, read_avg / baseline_cycles))
            write_pct = max(0.0, min(1.0, write_avg / baseline_cycles))

        percentages[cores] = { 'read_pct': read_pct, 'write_pct': write_pct }

    if percentages:
        print("\nPer-core wait percentages (of cycles per op):")
        for cores, p in sorted(percentages.items()):
            print(f"  {cores}: read {p['read_pct']*100:.1f}%, write {p['write_pct']*100:.1f}%")

    return percentages

def create_wait_stacked_bar(percentages_by_core, output_file="wait_pct_stacked_by_core.png"):
    """
    Create a stacked bar chart per core showing percentage of cycles per operation spent waiting
    for read vs write lock.
    """
    if not percentages_by_core:
        print("No wait percentage data to plot")
        return

    cores = sorted(percentages_by_core.keys())
    read_pcts = [percentages_by_core[c]['read_pct'] * 100.0 for c in cores]
    write_pcts = [percentages_by_core[c]['write_pct'] * 100.0 for c in cores]

    fig, ax = plt.subplots(figsize=(12, 8))
    ax.bar(cores, read_pcts, label='Read wait %')
    ax.bar(cores, write_pcts, bottom=read_pcts, label='Write wait %')

    ax.set_xlabel('Cores (from filename)', fontsize=12, fontweight='bold')
    ax.set_ylabel('Percent of cycles per operation spent waiting', fontsize=12, fontweight='bold')
    ax.set_title('Lock Wait Percentage by Core (Stacked Read/Write)', fontsize=14, fontweight='bold')
    ax.set_xticks(cores)
    ax.set_xticklabels([str(c) for c in cores])
    ax.set_ylim(0, 100)
    ax.grid(True, axis='y', alpha=0.3)

    ax.legend(fontsize=11)
    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Plot saved as {output_file}")
    plt.show()

def load_waits_by_core(out_dir):
    """
    Load read/write avg wait cycles for each core (file).

    Returns:
        dict[int, dict[str, float]]: cores -> { 'read_avg': float, 'write_avg': float }
    """
    out_path = Path(out_dir)
    txt_files = list(out_path.glob("*.txt"))
    waits_by_core = {}
    for file_path in txt_files:
        try:
            cores = int(file_path.stem)
        except ValueError:
            continue
        waits = parse_lock_wait_stats(file_path)
        waits_by_core[cores] = {
            'read_avg': waits['read_avg_cycles'],
            'write_avg': waits['write_avg_cycles'],
        }
    return waits_by_core

def create_wait_grouped_stacked_by_core(per_file_op_cycle_avgs, waits_by_core, output_file="wait_grouped_cycles_by_core.png"):
    """
    For each core, draw one bar per operation where bar height is avg cycles for that op.
    Each bar is stacked: write-wait, read-wait, other (remaining cycles).
    """
    if not per_file_op_cycle_avgs:
        print("No per-op cycle averages to plot")
        return

    cores = sorted(per_file_op_cycle_avgs.keys())
    # Determine union of ops across all cores
    ops = sorted({op for c in cores for op in per_file_op_cycle_avgs[c].keys()})

    # Visual layout params
    num_ops = len(ops)
    bar_width = 0.7 / max(1, num_ops)  # total group width ~0.7

    # Colors
    write_color = '#d62728'  # red-ish
    read_color = '#1f77b4'   # blue-ish
    other_color = '#2ca02c'  # green-ish

    fig, ax = plt.subplots(figsize=(14, 8))

    # X positions per core group
    x_indices = np.arange(len(cores))

    xtick_positions = []
    xtick_labels = []

    for op_idx, op in enumerate(ops):
        # Compute stacked components for each core
        total_vals = []
        write_vals = []
        read_vals = []
        other_vals = []

        for core in cores:
            total = float(per_file_op_cycle_avgs.get(core, {}).get(op, 0.0))
            waits = waits_by_core.get(core, {'read_avg': 0.0, 'write_avg': 0.0})
            write_wait = min(waits['write_avg'], total) if total > 0 else 0.0
            remaining = total - write_wait
            read_wait = min(waits['read_avg'], remaining) if remaining > 0 else 0.0
            other = max(total - (write_wait + read_wait), 0.0)

            total_vals.append(total)
            write_vals.append(write_wait)
            read_vals.append(read_wait)
            other_vals.append(other)

        # Bar positions offset per op within each core group
        positions = x_indices - 0.35 + (op_idx + 0.5) * bar_width

        # Draw stacked bars: first write, then read, then other on top
        ax.bar(positions, write_vals, width=bar_width, color=write_color, label='Write wait' if op_idx == 0 else None)
        ax.bar(positions, read_vals, width=bar_width, bottom=write_vals, color=read_color, label='Read wait' if op_idx == 0 else None)
        ax.bar(positions, other_vals, width=bar_width, bottom=np.array(write_vals) + np.array(read_vals), color=other_color, label='Other' if op_idx == 0 else None)

        # Operation labels above each group's middle can clutter; rely on legend and x-ticks
        for pos, core in zip(positions, cores):
            xtick_positions.append(pos)
            xtick_labels.append(f"{core}\n{op}")

    ax.set_xlabel('Cores (from filename)', fontsize=12, fontweight='bold')
    ax.set_ylabel('Average cycles per operation', fontsize=12, fontweight='bold')
    ax.set_title('Per-Core Per-Operation Cycles with Wait Breakdown', fontsize=14, fontweight='bold')
    # Label each bar by core and operation (two-line label)
    ax.set_xticks(xtick_positions)
    ax.set_xticklabels(xtick_labels)
    ax.grid(True, axis='y', alpha=0.3)

    # Build a second legend for ops using proxy artists (thin lines) to avoid clutter; instead, annotate on x-axis?
    # To keep it simple, show only wait categories in legend.
    handles, labels = ax.get_legend_handles_labels()
    # Deduplicate labels
    uniq = []
    seen = set()
    for h, l in zip(handles, labels):
        if l and l not in seen:
            uniq.append((h, l))
            seen.add(l)
    ax.legend([h for h, _ in uniq], [l for _, l in uniq], fontsize=11)

    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Plot saved as {output_file}")
    plt.show()

def main():
    """Main function to run the analysis."""
    # Get the directory containing this script
    script_dir = Path(__file__).parent
    out_dir = script_dir / "out"
    
    print(f"Analyzing output files in: {out_dir}")

    # Compute and plot per-operation average microseconds with x-axis as filename (cores)
    per_file_op_us_avgs, per_file_op_cycle_avgs = analyze_operation_avgs_per_file(out_dir)
    op_plot_file = script_dir / "operation_avg_us_plot.png"
    create_operation_avg_plot_by_file(per_file_op_us_avgs, str(op_plot_file))

    # Compute and plot grouped stacked bars per core per op (in cycles)
    waits_by_core = load_waits_by_core(out_dir)
    grouped_plot_file = script_dir / "wait_grouped_cycles_by_core.png"
    create_wait_grouped_stacked_by_core(per_file_op_cycle_avgs, waits_by_core, str(grouped_plot_file))

if __name__ == "__main__":
    main()
