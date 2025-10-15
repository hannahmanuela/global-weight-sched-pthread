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

def parse_operation_avgs_from_line(line):
    """
    Parse a single "cycles:" line to extract per-operation average cycle counts.

    Expected format examples:
        "0 cycles: sched 5157678(8455.21) enq 274464(1508.04) yield 1767400 (8579.61)"

    Returns:
        dict: op_name -> avg_cycles (float)
    """
    # Only process lines that look like "<coreNum> cycles: ..."
    if not re.match(r"^\d+\s+cycles:\s+", line):
        return {}

    # Remove leading "<num> cycles:" header
    line_after_header = re.sub(r"^\d+\s+cycles:\s+", "", line.strip())

    tokens = line_after_header.split()
    op_avgs = {}

    # Expect repeating groups: <opName> <sum(avg)> where avg is inside parentheses
    i = 0
    while i < len(tokens):
        op = tokens[i]
        # Next token may be like "12345(678.90)" OR "12345" followed by "(678.90)"
        if i + 1 >= len(tokens):
            break
        next_tok = tokens[i + 1]

        avg_value = None
        # Pattern case 1: sum(avg) in one token
        m = re.match(r"^\d+\(([-+]?[0-9]*\.?[0-9]+)\)$", next_tok)
        if m:
            avg_value = float(m.group(1))
            i += 2
        else:
            # Pattern case 2: sum and then a separate "(avg)" token
            if i + 2 < len(tokens):
                m2 = re.match(r"^\(([-+]?[0-9]*\.?[0-9]+)\)$", tokens[i + 2])
                if re.match(r"^\d+$", next_tok) and m2:
                    avg_value = float(m2.group(1))
                    i += 3
                else:
                    # Unable to parse this op group; advance 1 to avoid infinite loop
                    i += 1
                    continue
            else:
                i += 1
                continue

        op_avgs[op] = avg_value

    return op_avgs

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

    # Collect all averages per op across all cores and files
    op_to_avgs = {}

    for file_path in txt_files:
        try:
            with open(file_path, 'r') as f:
                for line in f:
                    op_avgs = parse_operation_avgs_from_line(line)
                    if not op_avgs:
                        continue
                    for op, avg in op_avgs.items():
                        op_to_avgs.setdefault(op, []).append(avg)
        except Exception as e:
            print(f"Error reading {file_path}: {e}")

    # Compute mean of averages per op
    op_mean_avgs = {op: float(np.mean(vals)) for op, vals in op_to_avgs.items() if vals}

    if op_mean_avgs:
        print("\nOperation average cycles across all cores/files:")
        for op, avg in sorted(op_mean_avgs.items()):
            print(f"  {op}: {avg:.2f}")

    return op_mean_avgs

def create_operation_avg_plot(op_mean_avgs, output_file="operation_avg_cycles_plot.png"):
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
    ax.set_ylabel('Average Cycles', fontsize=12, fontweight='bold')
    ax.set_title('Average Cycles per Operation (Averaged Across Cores)', fontsize=14, fontweight='bold')
    ax.grid(True, axis='y', alpha=0.3)
    plt.xticks(rotation=30, ha='right')
    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Plot saved as {output_file}")
    plt.show()

def analyze_operation_avgs_per_file(out_dir):
    """
    Compute per-file (core-count) average cycles per operation.

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
    per_file_op_avgs = {}

    for file_path in txt_files:
        # Extract number of cores from filename (e.g., "2.txt" -> 2)
        try:
            cores = int(file_path.stem)
        except ValueError:
            print(f"Warning: Could not extract core count from filename {file_path.name}")
            continue

        op_to_vals = {}
        try:
            with open(file_path, 'r') as f:
                for line in f:
                    op_avgs = parse_operation_avgs_from_line(line)
                    if not op_avgs:
                        continue
                    for op, avg in op_avgs.items():
                        op_to_vals.setdefault(op, []).append(avg)
        except Exception as e:
            print(f"Error reading {file_path}: {e}")
            continue

        # Mean per op for this file
        per_file_op_avgs[cores] = {op: float(np.mean(vals)) for op, vals in op_to_vals.items() if vals}

    if per_file_op_avgs:
        print("\nPer-file operation averages:")
        for cores, op_map in sorted(per_file_op_avgs.items()):
            ops_str = ", ".join(f"{op}:{avg:.2f}" for op, avg in sorted(op_map.items()))
            print(f"  {cores}: {ops_str}")

    return per_file_op_avgs

def create_operation_avg_plot_by_file(per_file_op_avgs, output_file="operation_avg_cycles_plot.png"):
    """
    Create and save a plot where the x-axis is the file name (core count),
    and each operation is a colored line showing average cycles per core count.

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
    ax.set_ylabel('Average Cycles', fontsize=12, fontweight='bold')
    ax.set_title('Average Cycles per Operation by Core Count', fontsize=14, fontweight='bold')
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

def main():
    """Main function to run the analysis."""
    # Get the directory containing this script
    script_dir = Path(__file__).parent
    out_dir = script_dir / "out"
    
    print(f"Analyzing output files in: {out_dir}")
    
    # Analyze the output files
    data = analyze_output_directory(out_dir)
    
    if not data:
        print("No valid data found. Exiting.")
        return
    
    print(f"\nFound data for {len(data)} core configurations:")
    for cores, failures in data.items():
        print(f"  {cores} cores: {failures}")
    
    # Create and save the lock failures plot
    output_file = script_dir / "lock_failures_plot.png"
    create_plot(data, str(output_file))

    # Compute and plot per-operation average cycles with x-axis as filename (cores)
    per_file_op_avgs = analyze_operation_avgs_per_file(out_dir)
    op_plot_file = script_dir / "operation_avg_cycles_plot.png"
    create_operation_avg_plot_by_file(per_file_op_avgs, str(op_plot_file))

if __name__ == "__main__":
    main()
