#!/usr/bin/env python3

"""Wrapper for matplotlib to create various plots (multi values, multi panes)

see README.md

"""

import argparse
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
from collections import defaultdict
from typing import Dict, List, Tuple, Any, Optional, Union
import numpy as np
import shlex
from matplotlib.ticker import FixedLocator, FuncFormatter, MultipleLocator, NullLocator

def get_available_palettes() -> List[str]:
    """Get list of available matplotlib color palettes/colormaps"""
    return sorted(plt.colormaps())

def get_legend_positions() -> List[str]:
    """Get list of available matplotlib legend positions"""
    return [
        'best', 'upper right', 'upper left', 'lower left', 'lower right',
        'right', 'center left', 'center right', 'lower center',
        'upper center', 'center'
    ]

def str2bool(v: Union[str, bool]) -> bool:
    """Convert string to boolean"""
    if isinstance(v, bool):
        return v
    if v.lower() in ('yes', 'true', 't', 'y', '1'):
        return True
    elif v.lower() in ('no', 'false', 'f', 'n', '0'):
        return False
    else:
        raise argparse.ArgumentTypeError('Boolean value expected.')

def parse_plot_options(option_string: str) -> Dict[str, Any]:
    """Parse plot-specific options from @New plot line"""
    options: Dict[str, Any] = {}
    if ':' not in option_string:
        return options
    _, opts_part = option_string.split(':', 1)
    try:
        tokens = shlex.split(opts_part)
    except ValueError:
        tokens = opts_part.split()
    for token in tokens:
        if '=' in token:
            key, value = token.split('=', 1)
            key = key.strip()
            value = value.strip()
            if key in ['logx', 'logy', 'symlogy', 'normalize', 'grid', 'grid_major', 'grid_minor', 'hzticks']:
                try:
                    options[key] = str2bool(value)
                except:
                    options[key] = value.lower() in ('true', '1', 'yes')
            elif key in ['miny', 'maxy', 'linewidth', 'linthreshy', 'ystep']:
                try:
                    options[key] = float(value)
                except:
                    pass
            elif key in ['width', 'height']:
                try:
                    options[key] = int(value)
                except:
                    pass
            else:
                options[key] = value  # Handle title here
    return options

def setup_theme(inverse: bool) -> None:
    """Setup the plot theme (normal or inverse)"""
    if inverse:
        plt.style.use('dark_background')
    else:
        plt.style.use('default')

def setup_grid(ax: plt.Axes, args: argparse.Namespace, plot_options: Optional[Dict[str, Any]] = None) -> None:
    """Setup grid for the axes"""
    if plot_options is None:
        plot_options = {}
    grid_enabled = plot_options.get('grid', getattr(args, 'grid', False))
    grid_major = plot_options.get('grid_major', getattr(args, 'grid_major', True))
    grid_minor = plot_options.get('grid_minor', getattr(args, 'grid_minor', False))
    if grid_enabled or grid_major or grid_minor:
        if grid_minor:
            ax.minorticks_on()
        if grid_major:
            ax.grid(True, which='major', alpha=0.7, linewidth=0.8)
        if grid_minor:
            ax.grid(True, which='minor', alpha=0.3, linewidth=0.5, linestyle=':')

def normalize_data(data: Dict[str, List[Dict[str, List[float]]]]) -> Dict[str, List[Dict[str, List[float]]]]:
    """Normalize data to range [0, 1], sharing one x/y range across all of a name's runs"""
    normalized_data: Dict[str, List[Dict[str, List[float]]]] = {}
    for name, series_list in data.items():
        all_x = np.array([v for s in series_list for v in s['x']])
        all_y = np.array([v for s in series_list for v in s['y']])
        x_min, x_max = (all_x.min(), all_x.max()) if len(all_x) > 1 else (0.0, 1.0)
        y_min, y_max = (all_y.min(), all_y.max()) if len(all_y) > 1 else (0.0, 1.0)
        new_series_list = []
        for values in series_list:
            x_vals = np.array(values['x'])
            y_vals = np.array(values['y'])
            x_norm = (x_vals - x_min) / (x_max - x_min) if x_max != x_min else x_vals
            y_norm = (y_vals - y_min) / (y_max - y_min) if y_max != y_min else y_vals
            new_series_list.append({'x': x_norm.tolist(), 'y': y_norm.tolist()})
        normalized_data[name] = new_series_list
    return normalized_data

def parse_data_file(filename: str) -> List[Tuple[Dict[str, List[Dict[str, List[float]]]], Dict[str, Any]]]:
    """Parse the input txt file and return list of plot groups with their options.

    A #name seen again within the same plot (no intervening @New plot) starts a new,
    separate run under that same name - overlaid as its own line sharing one colour and
    legend entry, rather than concatenated onto the previous run's data (which would draw
    one line zigzagging between the two runs' x-ranges).
    """
    plots: List[Tuple[Dict[str, List[Dict[str, List[float]]]], Dict[str, Any]]] = []
    current_plot_data: Dict[str, List[Dict[str, List[float]]]] = {}
    current_plot_options: Dict[str, Any] = {}
    current_name: Optional[str] = None
    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith('@New plot') or line.startswith('@new plot'):
                if current_plot_data:
                    plots.append((current_plot_data, current_plot_options))
                current_plot_data = {}
                current_plot_options = parse_plot_options(line)
                continue
            if line.startswith('#'):
                current_name = line[1:].strip()
                current_plot_data.setdefault(current_name, []).append({'x': [], 'y': []})
            else:
                if current_name is not None:
                    try:
                        x, y = map(float, line.split())
                        current_plot_data[current_name][-1]['x'].append(x)
                        current_plot_data[current_name][-1]['y'].append(y)
                    except ValueError:
                        print(f"Warning: Could not parse line: {line}")
                        continue
    if current_plot_data:
        plots.append((current_plot_data, current_plot_options))
    return plots if plots else [({}, {})]

OCTAVE_CENTERS_HZ = [0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0]


def format_hz(value: float, _pos: Optional[int] = None) -> str:
    """Plain frequency label: 250, 1k, 16k"""
    return f"{value / 1000:g}k" if value >= 1000 else f"{value:g}"


def apply_octave_hz_ticks(ax: plt.Axes) -> None:
    """Label a log x-axis at octave-spaced ISO centres, so one grid cell is one octave"""
    low, high = ax.get_xlim()
    ax.xaxis.set_major_locator(FixedLocator([f for f in OCTAVE_CENTERS_HZ if low <= f <= high]))
    ax.xaxis.set_major_formatter(FuncFormatter(format_hz))
    ax.xaxis.set_minor_locator(NullLocator())


def create_plots(data_plots: List[Tuple[Dict[str, Dict[str, List[float]]], Dict[str, Any]]], args: argparse.Namespace) -> None:
    """Create and save the plots"""
    n_plots = len(data_plots)
    setup_theme(args.inverse)
    if n_plots == 1:
        fig, ax = plt.subplots(figsize=(args.width/100, args.height/100))
        axes = [ax]
    else:
        cols = args.cols if args.cols > 0 else int(np.ceil(np.sqrt(n_plots)))
        rows = int(np.ceil(n_plots / cols))
        fig, axes = plt.subplots(rows, cols, figsize=(args.width/100 * cols, args.height/100 * rows))
        axes = axes.flatten() if hasattr(axes, 'flatten') else axes
    for plot_idx, (data, plot_options) in enumerate(data_plots):
        if plot_idx >= len(axes):
            break
        ax = axes[plot_idx] if n_plots > 1 else axes[0]
        effective_options = vars(args).copy()
        effective_options.update(plot_options)
        palette = effective_options.get('palette', args.palette)
        try:
            cmap = plt.get_cmap(palette)
        except ValueError:
            print(f"Warning: Palette '{palette}' not found. Using default 'Set1'.")
            cmap = plt.get_cmap('Set1')
        if effective_options.get('normalize', False):
            data = normalize_data(data)
        # Qualitative palettes (Set1 etc.) are a short, discrete colour list meant to be
        # indexed by position - cmap(int) does that. Stretching a float 0..1 across the
        # whole map instead (cmap(idx / (len(data)-1))) picks each colour's position along
        # the *palette*, not its position in the list - for exactly 2 series that lands on
        # index 0 and 1.0, i.e. the palette's first and *last* entries, which for Set1 is a
        # washed-out grey rather than a second strong colour. Continuous colormaps (viridis
        # etc.) still need the float form to spread smoothly across the gradient.
        is_qualitative = isinstance(cmap, mcolors.ListedColormap)
        color_idx = 0
        for name, series_list in data.items():
            non_empty = [s for s in series_list if s['x'] and s['y']]
            if not non_empty:
                continue
            if is_qualitative:
                color = cmap(color_idx % cmap.N)
            else:
                color = cmap(color_idx / max(1, len(data) - 1)) if len(data) > 1 else cmap(0)
            linewidth = effective_options.get('linewidth', args.linewidth)
            # Repeated runs under the same name (see parse_data_file) share this one colour
            # and get a single legend entry, on the first run only.
            for i, values in enumerate(non_empty):
                ax.plot(values['x'], values['y'], label=name if i == 0 else None, color=color, linewidth=linewidth)
            color_idx += 1
        if effective_options.get('logx', False):
            ax.set_xscale('log')
            if effective_options.get('hzticks', False):
                apply_octave_hz_ticks(ax)
        if effective_options.get('logy', False):
            ax.set_yscale('log')
        elif effective_options.get('symlogy', False):
            linthreshy = effective_options.get('linthreshy', args.linthreshy)
            ax.set_yscale('symlog', linthresh=linthreshy)
        ax.set_aspect(effective_options.get('aspect', args.aspect))
        setup_grid(ax, args, plot_options)
        labelx = effective_options.get('labelx', args.labelx)
        labely = effective_options.get('labely', args.labely)
        ax.set_xlabel(labelx)
        ax.set_ylabel(labely)
        miny = effective_options.get('miny', args.miny)
        maxy = effective_options.get('maxy', args.maxy)
        if miny is not None or maxy is not None:
            current_ylim = ax.get_ylim()
            ymin = miny if miny is not None else current_ylim[0]
            ymax = maxy if maxy is not None else current_ylim[1]
            ax.set_ylim(ymin, ymax)
        ystep = effective_options.get('ystep', None)
        if ystep:
            ax.yaxis.set_major_locator(MultipleLocator(ystep))
        # Add legend if there are multiple series
        if len(data) > 1:
            legendpos = effective_options.get('legendpos', args.legendpos)
            ax.legend(loc=legendpos)
        # Add title if specified in options
        title = effective_options.get('title', None)
        if title is not None:
            ax.set_title(title)
        elif n_plots > 1:
            ax.set_title(f'Plot {plot_idx + 1}')
    if n_plots > 1:
        for i in range(n_plots, len(axes)):
            axes[i].set_visible(False)
    plt.tight_layout()
    try:
        plt.savefig(args.outfile, dpi=150, bbox_inches='tight')
        print(f"Plot saved to: {args.outfile}")
    except Exception as e:
        print(f"Error saving plot: {e}")
    plt.close()

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Plot multiple groups of 2D data from a text file with grid and theme support",
        epilog=f"""
Available palettes: {', '.join(get_available_palettes()[:20])}... (and many more)
Legend positions: {', '.join(get_legend_positions())}
Text file format:
#group1
x1 y1
x2 y2
#group2
x3 y3
x4 y4

@New plot: labelx="Frequency (Hz)" labely="Magnitude (dB)" title="Frequency Response" logx=true logy=false grid=true
#group3
x5 y5

A #name repeated within the same plot starts a new, separate overlapping run under that
name (its own line, not concatenated onto the previous run) - all runs share one colour
and one legend entry. Useful for plotting several noisy trials of the same measurement
together, e.g. drawn before a final theoretical/reference #name so it renders on top:
#empirical
0 0.1
1 0.4
#empirical
0 -0.2
1 0.5
#theoretical
0 0.0
1 0.45

Plot-specific options (use in @New plot lines):
- labelx, labely: axis labels
- title: plot title
- logx, logy: logarithmic scales (true/false)
- symlogy, linthreshy: symmetric-log y-axis (true/false) and its linear half-width around zero,
  for data that crosses zero but spans orders of magnitude (ignored if logy is also set)
- normalize: normalize data (true/false)
- hzticks: with logx, label the x-axis at octave-spaced ISO centres (63, 125, ... 1k, 2k, ...)
  as plain Hz, one grid cell per octave (true/false)
- miny, maxy: y-axis limits (numbers)
- ystep: y-axis major tick spacing (number), e.g. 24 to line dB/octave slopes up with the grid
- linewidth: line width (number)
- palette: color palette name
- legendpos: legend position
- grid: enable grid (true/false)
- grid_major: enable major grid (true/false)
- grid_minor: enable minor grid (true/false)
- aspect: 'auto' or 'equal'
""",
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument('-f', '--infile', required=True, help='Input text file with data')
    parser.add_argument('-o', '--outfile', default='plot.svg', help='Output file (can be png, svg, pdf, etc.) (default: plot.svg)')
    parser.add_argument('-w', '--width', type=int, default=1200, help='Width of the plot in pixels (default: 1200)')
    parser.add_argument('--height', type=int, default=800, help='Height of the plot in pixels (default: 800)')
    parser.add_argument('-n', '--normalize', action='store_true', help='Normalize values to [0,1] range')
    parser.add_argument('--labelx', default='x', help='Label for x-axis (default: "x")')
    parser.add_argument('--labely', default='y', help='Label for y-axis (default: "y")')
    parser.add_argument('--palette', default='Set1', help='Color palette name from matplotlib (default: "Set1")')
    parser.add_argument('--legendpos', default='best', choices=get_legend_positions(), help='Legend position (default: "best")')
    parser.add_argument('--linewidth', type=float, default=1.0, help='Line width for plotting (default: 1.0)')
    parser.add_argument('--miny', type=float, help='Minimum y-axis value (default: auto)')
    parser.add_argument('--maxy', type=float, help='Maximum y-axis value (default: auto)')
    parser.add_argument('--logx', action='store_true', help='Use logarithmic scale for x-axis')
    parser.add_argument('--logy', action='store_true', help='Use logarithmic scale for y-axis')
    parser.add_argument('--hzticks', action='store_true', help='With --logx: label the x-axis at octave-spaced ISO centres as plain Hz')
    parser.add_argument('--symlogy', action='store_true', help='Use symmetric-log scale for y-axis: linear near zero, logarithmic beyond --linthreshy. For data that crosses zero but spans orders of magnitude (ignored if --logy is also set)')
    parser.add_argument('--linthreshy', type=float, default=1.0, help='Linear region half-width around zero for --symlogy (default: 1.0)')
    parser.add_argument('--aspect', default='auto', choices=['auto', 'equal'], help="Axes aspect ratio: 'equal' keeps x/y units the same size, e.g. for orbit/phase-plane plots (default: 'auto')")
    parser.add_argument('--cols', type=int, default=0, help='Force this many columns when there are multiple @New plot sections, e.g. --cols 1 to stack them vertically (default: 0, auto square-ish grid)')
    parser.add_argument('--grid', action='store_true', help='Enable grid (same as --grid-major)')
    parser.add_argument('--grid-major', action='store_true', default=True, help='Enable major grid lines (default: True)')
    parser.add_argument('--grid-minor', action='store_true', help='Enable minor grid lines (default: False)')
    parser.add_argument('--no-grid', action='store_true', help='Disable all grid lines')
    parser.add_argument('--inverse', action='store_true', help='Use dark theme (white on black)')
    args = parser.parse_args()
    if args.no_grid:
        args.grid_major = False
        args.grid_minor = False
    elif args.grid:
        args.grid_major = True
    try:
        data_plots = parse_data_file(args.infile)
        if not data_plots or not any(data[0] for data in data_plots):
            print("No data found in input file")
            return
        create_plots(data_plots, args)
    except FileNotFoundError:
        print(f"Error: File '{args.infile}' not found")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()
