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
    
    # Create and save the plot
    output_file = script_dir / "lock_failures_plot.png"
    create_plot(data, str(output_file))

if __name__ == "__main__":
    main()
