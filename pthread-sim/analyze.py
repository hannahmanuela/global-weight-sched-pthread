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
                    # Extract the lock failure data
                    # Format: "failed glist trylock min X time Y list Z"
                    parts = line.strip().split()
                    
                    # Find the lock types and their failure counts
                    for i, part in enumerate(parts):
                        if part in ['min', 'time', 'list']:
                            if i + 1 < len(parts):
                                try:
                                    count = int(parts[i + 1])
                                    lock_failures[part] = count
                                except ValueError:
                                    print(f"Warning: Could not parse count for {part} in {file_path}")
                                    lock_failures[part] = 0
                    
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
    lock_types = ['min', 'time', 'list']
    
    # Create figure and axis
    fig, ax = plt.subplots(figsize=(12, 8))
    
    # Set up colors for different lock types
    colors = {'min': 'red', 'time': 'blue', 'list': 'green'}
    
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
    max_failures = max(max(data[core].values()) for core in cores if data[core])
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
