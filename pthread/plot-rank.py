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
outfile = os.path.join(directory, "plot-rank.gp")
dat_files = sorted(glob.glob(os.path.join(directory, "rankprio-*.dat")))

if not dat_files:
    print("No rankprio-*.dat files found.")
    sys.exit(1)

n = len(dat_files)
width = 0.8 / n

lines = []
lines.append("set terminal png size 1200,800")
lines.append(f"set output '{os.path.join(directory, 'plot-rank.png')}'")
lines.append("set key top right")
lines.append("set grid")
lines.append("set xlabel 'rank error'")
lines.append("set ylabel 'count'")
lines.append("set logscale y")
lines.append("set style fill solid 0.8 border -1")
lines.append(f"set boxwidth {width}")
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

# Build plot command: offset each dataset's bars so they cluster side by
# side around each integer rank-error value instead of overlapping.
plot_parts = []
for i, f in enumerate(dat_files):
    stem = os.path.splitext(os.path.basename(f))[0]
    varname = "$" + re.sub(r'[^a-zA-Z0-9]', '_', stem).upper()
    offset = (i - (n - 1) / 2.0) * width
    plot_parts.append(f'{varname} using ($1+({offset})):2 with boxes title "{stem}"')

lines.append("plot " + ", \\\n     ".join(plot_parts))

with open(outfile, "w") as fh:
    fh.write("\n".join(lines) + "\n")
