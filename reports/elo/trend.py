#!/usr/bin/env python3
"""Redraw reports/elo/trend.svg from reports/elo/history.tsv.

Standard library only. history.tsv is tab-separated with header:
  date  selfplay_score  elo_delta  abs_elo  abs_margin  notes
Rows with a missing/non-numeric absolute Elo are skipped in the plot.
"""
import csv
import os

HERE = os.path.dirname(os.path.abspath(__file__))
TSV = os.path.join(HERE, "history.tsv")
OUT = os.path.join(HERE, "trend.svg")

W, H = 640, 360
ML, MR, MT, MB = 60, 20, 30, 50
PW, PH = W - ML - MR, H - MT - MB


def main():
    rows = []
    with open(TSV, newline="") as f:
        r = csv.reader(f, delimiter="\t")
        next(r, None)
        for row in r:
            if len(row) < 5:
                continue
            try:
                elo = float(row[3])
                marg = float(row[4]) if row[4].strip() else 0.0
            except (ValueError, IndexError):
                continue
            rows.append((row[0], elo, marg))
    if not rows:
        raise SystemExit("no plottable rows in history.tsv")

    los = [e - m for _, e, m in rows]
    his = [e + m for _, e, m in rows]
    ymin = min(los)
    ymax = max(his)
    if ymax - ymin < 100:
        c = (ymax + ymin) / 2
        ymin, ymax = c - 50, c + 50
    pad = (ymax - ymin) * 0.1
    ymin -= pad
    ymax += pad

    n = len(rows)

    def x(i):
        return ML + (PW * i / (n - 1) if n > 1 else PW / 2)

    def y(v):
        return MT + PH * (ymax - v) / (ymax - ymin)

    svg = []
    svg.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
               f'viewBox="0 0 {W} {H}" font-family="sans-serif" font-size="12">')
    svg.append(f'<rect width="{W}" height="{H}" fill="white"/>')
    svg.append(f'<text x="{W/2}" y="18" text-anchor="middle" font-size="15" '
               f'font-weight="bold">Seger absolute Elo over time</text>')
    steps = 5
    for k in range(steps + 1):
        v = ymin + (ymax - ymin) * k / steps
        yy = y(v)
        svg.append(f'<line x1="{ML}" y1="{yy:.1f}" x2="{ML+PW}" y2="{yy:.1f}" '
                   f'stroke="#ddd" stroke-width="1"/>')
        svg.append(f'<text x="{ML-6}" y="{yy+4:.1f}" text-anchor="end">{v:.0f}</text>')
    svg.append(f'<line x1="{ML}" y1="{MT}" x2="{ML}" y2="{MT+PH}" stroke="#333"/>')
    svg.append(f'<line x1="{ML}" y1="{MT+PH}" x2="{ML+PW}" y2="{MT+PH}" stroke="#333"/>')
    svg.append(f'<text x="{ML+PW/2}" y="{H-8}" text-anchor="middle">report date</text>')
    for i, (date, elo, marg) in enumerate(rows):
        xx = x(i)
        if marg > 0:
            svg.append(f'<line x1="{xx:.1f}" y1="{y(elo-marg):.1f}" '
                       f'x2="{xx:.1f}" y2="{y(elo+marg):.1f}" stroke="#888"/>')
            for v in (elo - marg, elo + marg):
                svg.append(f'<line x1="{xx-4:.1f}" y1="{y(v):.1f}" '
                           f'x2="{xx+4:.1f}" y2="{y(v):.1f}" stroke="#888"/>')
    pts = " ".join(f"{x(i):.1f},{y(e):.1f}" for i, (_, e, _) in enumerate(rows))
    svg.append(f'<polyline points="{pts}" fill="none" stroke="#1f77b4" stroke-width="2"/>')
    for i, (date, elo, _) in enumerate(rows):
        xx, yy = x(i), y(elo)
        svg.append(f'<circle cx="{xx:.1f}" cy="{yy:.1f}" r="3.5" fill="#1f77b4"/>')
        svg.append(f'<text x="{xx:.1f}" y="{MT+PH+16}" text-anchor="middle" '
                   f'font-size="10">{date}</text>')
    svg.append('</svg>')

    with open(OUT, "w") as f:
        f.write("\n".join(svg) + "\n")
    print(f"wrote {OUT} with {n} point(s)")


if __name__ == "__main__":
    main()
