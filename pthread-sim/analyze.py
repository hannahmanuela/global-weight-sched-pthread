

#!/usr/bin/env python3
"""
Analysis script to plot the distribution of overall throughput for one pinning policy.
Usage: analyze.py <dir>   (e.g. out/seq, out/numa-first, out/phys-first)
X-axis: number of cores (subdir name)
Y-axis: overall throughput (iterations/second)
"""

import os
import re
import sys
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path
import matplotlib.ticker as ticker

plt.rcParams['font.size'] = 18

def extract_throughput_from_file(filepath):
    """Extract overall throughput from a file with format 'tp X.XXM/s'."""
    try:
        with open(filepath, 'r') as f:
            for line in f:
                # Match pattern: "tp X.XXM/s" where X.XX is the throughput in millions per second
                match = re.search(r'tp\s+([\d.]+)M/s', line)
                if match:
                    throughput_millions = float(match.group(1))
                    # Convert from millions per second to actual throughput
                    return throughput_millions * 1_000_000
    except FileNotFoundError:
        print(f"Warning: {filepath} not found")
    except Exception as e:
        print(f"Error reading {filepath}: {e}")
    return None

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <dir>")
        sys.exit(1)
    out_dir = Path(sys.argv[1])
    if not out_dir.exists():
        print(f"Error: {out_dir} directory not found")
        return
    
    # Collect data: {num_cores: [list of overall throughputs]}
    data = {}
    core_counts = []
    
    # Iterate through subdirectories in the out directory (one per core count)
    for subdir in sorted(out_dir.iterdir()):
        if not subdir.is_dir():
            continue

        # Try to parse directory name as number of cores (e.g., "4" -> 4)
        try:
            num_cores = int(subdir.name)
        except ValueError:
            print(f"Warning: Skipping non-numeric directory: {subdir.name}")
            continue

        filepath = subdir / 'vals.txt'
        if not filepath.exists():
            print(f"Warning: {filepath} not found")
            continue

        # Extract overall throughput from the file
        overall_throughput = extract_throughput_from_file(filepath)
        
        if overall_throughput is not None:
            # Store in data dictionary
            if num_cores not in data:
                data[num_cores] = []
                core_counts.append(num_cores)
            data[num_cores].append(overall_throughput)
    
    if not data:
        print("No data found to plot")
        return
    
    # Sort by number of cores
    core_counts = sorted(core_counts)
    
    # Prepare data for plotting
    plot_data = [data[cores] for cores in core_counts]
    
    # Create the plot
    fig, ax = plt.subplots(figsize=(12, 6))
    
    ax.scatter(core_counts, [np.mean(data[cores]) for cores in core_counts], color='blue', s=100,
                  marker='s', label='Overall Throughput', alpha=0.7, zorder=3)
    ax.plot(core_counts, [np.mean(data[cores]) for cores in core_counts], color='blue',
               linestyle='--', alpha=0.5, linewidth=1, zorder=2)
    
    ax.set_xlabel('Number of Cores')
    ax.set_ylabel('Overall Throughput (ops/s)')
    ax.set_title(f'Overall Throughput by Number of Cores ({out_dir.name})')
    ax.set_ylim(bottom=0)  # Set y-axis lower limit to 0
    ax.grid(True, alpha=0.3, axis='y')

    def format_sci_clean(x, pos):
        """Format numbers in clean scientific notation."""
        if abs(x) < 1e-10:
            return '0'
        # Use fewer decimal places for cleaner look
        exp = int(np.floor(np.log10(abs(x))))
        coeff = x / (10 ** exp)
        # Show 1-2 significant digits
        if abs(coeff) >= 10:
            return f'{coeff:.0f}e{exp:+d}'
        else:
            return f'{coeff:.1f}e{exp:+d}'
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(format_sci_clean))
    
    plt.tight_layout()
    out_path = out_dir / 'throughput_distribution.png'
    plt.savefig(out_path, dpi=150)
    print(f"Plot saved to {out_path}")
    
    # Also print summary statistics
    print("\nSummary Statistics (overall throughput in iterations/second):")
    print("-" * 60)
    for cores in core_counts:
        throughputs = data[cores]
        print(f"Cores {cores:2d}: mean={np.mean(throughputs):.2e}, "
              f"median={np.median(throughputs):.2e}, "
              f"min={np.min(throughputs):.2e}, "
              f"max={np.max(throughputs):.2e}, "
              f"count={len(throughputs)}")

if __name__ == '__main__':
    main()

