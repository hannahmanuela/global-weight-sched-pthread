#!/usr/bin/env python3
import glob
import os
import re
import sys

def parse_value(s):
    """Strip trailing non-numeric chars (e.g. 'M/s', 'ms') and return float."""
    m = re.match(r'[-+]?\d*\.?\d+', s)
    if m:
        return float(m.group())
    return None

if len(sys.argv) < 2:
    print(f"Usage: {sys.argv[0]} <directory>")
    sys.exit(1)

directory = sys.argv[1]
outfile = os.path.join(directory, "plot-tp.gp")
dat_files = sorted(glob.glob(os.path.join(directory, "tp-*.dat")))

if not dat_files:
    print("No .dat files found.")
    sys.exit(1)

lines = []
lines.append("set terminal png size 1200,800")
lines.append(f"set output '{os.path.join(directory, 'plot-tp.png')}'")
lines.append("set key top right")
lines.append("set grid")
lines.append("")

# Embed cleaned data as gnuplot inline blocks
for f in dat_files:
    stem = os.path.splitext(os.path.basename(f))[0]
    varname = "$" + re.sub(r'[^a-zA-Z0-9]', '_', stem).upper()
    lines.append(f"{varname} << EOD")
    with open(f) as fh:
        for line in fh:
            parts = line.split()
            if len(parts) >= 2:
                x = parse_value(parts[0])
                y = parse_value(parts[1])
                if x is not None and y is not None:
                    lines.append(f"{x} {y}")
    lines.append("EOD")
    lines.append("")

# Build plot command
plot_parts = []
for f in dat_files:
    stem = os.path.splitext(os.path.basename(f))[0]
    varname = "$" + re.sub(r'[^a-zA-Z0-9]', '_', stem).upper()
    plot_parts.append(f'{varname} using 1:2 with linespoints title "{stem}"')

lines.append("plot " + ", \\\n     ".join(plot_parts))

with open(outfile, "w") as fh:
    fh.write("\n".join(lines) + "\n")
