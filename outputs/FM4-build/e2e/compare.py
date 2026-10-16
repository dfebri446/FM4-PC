"""Compare two e2e run directories (each with summary.json) and print a PASS/WARN/FAIL verdict.
Usage: compare.py <baseline> <candidate> [--json]. Exit 1 on FAIL, 2 on bad input, else 0.
Accepts single-run and aggregate (repeat) summaries; missing keys show as "-".
"""
import json
import os
import statistics
import sys

# Candidate FPS more than 10% below baseline -> FAIL. Two --repeat 3 runs of the same binary
# and cvars differed by 7% in fps_median (per-run medians 30.0/26.1/25.4 vs 24.2/29.3/24.1),
# so a tighter threshold fails on noise. fps_mean is used when both summaries have it.
FPS_DROP_FAIL = 0.10
P99_RISE_FAIL = 0.20       # candidate frame_ms_p99 more than 20% above baseline -> FAIL
WAIT_RISE_REL = 0.20       # wait cause rose by more than 20% ...
WAIT_RISE_ABS = 5.0        # ... and by more than 5 ms/s absolute -> WARN
LUMA_REL_WARN = 0.10       # capture luma_mean relative difference -> WARN
BLACK_SAT_PP_WARN = 5.0    # black_pct / saturated_pct difference in percentage points -> WARN
HIST_L1_WARN = 20.0        # luma_hist16 L1 distance in percentage points -> WARN
CAPTURE_NOTE = "races are not deterministic; check captures by eye"
SCALARS = ["fps_mean", "fps_median", "fps_min", "frame_ms_p50", "frame_ms_p95", "frame_ms_p99",
           "hitch_50ms_per_min", "hitch_100ms_per_min", "cp_busy_ms_per_s_median",
           "readbacks_per_s_median", "draws_per_s_median", "gpu_frame_ms_median",
           "gpu_resolve_ms_median", "gpu_span_ms_median"]
CAPTURE_KEYS = ["luma_mean", "black_pct", "saturated_pct"]
# Measured phases of a scenario (summary.json "phases", e.g. race, photo, replay). They last
# 20-40 s, so their frame p99 only warns.
PHASE_KEYS = ["fps_mean", "frame_ms_p99", "cp_busy_ms_per_s_median", "gpu_frame_ms_median"]


def load(path):
    """Read summary.json from a run directory; exit 2 with a message if missing or invalid."""
    f = os.path.join(path, "summary.json")
    if not os.path.isfile(f):
        print(f"error: summary.json not found: {f}", file=sys.stderr)
        sys.exit(2)
    try:
        with open(f, encoding="utf-8") as fh:
            return json.load(fh)
    except json.JSONDecodeError as e:
        print(f"error: invalid JSON in {f}: {e}", file=sys.stderr)
        sys.exit(2)


def num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def median_capture(caps):
    """Per-key median over the non-null captures of an aggregate run."""
    caps = [c for c in caps if c]
    if not caps:
        return None
    out = {}
    for k in CAPTURE_KEYS:
        vals = [c[k] for c in caps if num(c.get(k))]
        out[k] = statistics.median(vals) if vals else None
    hists = [h for h in (c.get("luma_hist16") for c in caps) if isinstance(h, list)]
    if hists:
        hists = [h for h in hists if len(h) == len(hists[0])]
        out["luma_hist16"] = [statistics.median(col) for col in zip(*hists)]
    return out


def normalize(s):
    """Return a copy in single-run shape: derive final_capture and errors when absent."""
    d = dict(s)
    if "final_captures" in d:
        d["final_capture"] = median_capture(d.pop("final_captures") or [])
    d["errors"] = d.get("errors") or dict(d.get("top_errors") or [])
    return d


def fmt(v):
    return "-" if v is None else f"{v:.2f}" if isinstance(v, float) else str(v)


def row(metric, b, c):
    d = c - b if num(b) and num(c) else None
    pct = d / abs(b) * 100 if d is not None and b else None
    return {"metric": metric, "baseline": b, "candidate": c, "delta": d, "delta_pct": pct}


def hist_l1(bh, ch):
    if isinstance(bh, list) and isinstance(ch, list) and len(bh) == len(ch):
        return sum(abs(x - y) for x, y in zip(bh, ch))
    return None


def build_rows(b, c):
    rows = [row(k, b.get(k), c.get(k)) for k in SCALARS]
    for key in ("wait_ms_per_s_median", "cpu_ms_per_s_median", "counts_per_frame_median"):
        bd, cd = b.get(key) or {}, c.get(key) or {}
        rows += [row(f"{key}.{k}", bd.get(k), cd.get(k)) for k in sorted(set(bd) | set(cd))]
    bc, cc = b.get("final_capture") or {}, c.get("final_capture") or {}
    rows += [row(f"capture.{k}", bc.get(k), cc.get(k)) for k in CAPTURE_KEYS]
    l1 = hist_l1(bc.get("luma_hist16"), cc.get("luma_hist16"))
    rows.append({"metric": "capture.luma_hist16 L1 (pp)", "baseline": None, "candidate": None,
                 "delta": l1, "delta_pct": None})
    bp, cp = b.get("phases") or {}, c.get("phases") or {}
    for name in [n for n in bp if n in cp]:
        rows += [row(f"phase.{name}.{k}", bp[name].get(k), cp[name].get(k)) for k in PHASE_KEYS]
    return rows


def pair(b, c, k):
    """(baseline, candidate) for key k when both are numeric, else None."""
    bv, cv = b.get(k), c.get(k)
    return (bv, cv) if num(bv) and num(cv) else None


def rel_change(b, c):
    """Relative change of c vs b; inf when b is zero and c is not."""
    if b == 0:
        return 0.0 if c == 0 else float("inf")
    return abs(c - b) / abs(b)


def check_fail(b, c):
    fail = []
    if c.get("crashed"):
        fail.append(f"candidate crashed: {c['crashed']}")
    if c.get("goal_reached") is False:
        steps = c.get("failed_steps") or [c.get("failed_step")]
        fail.append("candidate goal_reached is False" +
                    "".join(f"; {step}" for step in steps if step))
    key = "fps_mean" if pair(b, c, "fps_mean") else "fps_median"
    p = pair(b, c, key)
    if p and p[1] < p[0] * (1 - FPS_DROP_FAIL):
        fail.append(f"{key} {p[1]:.2f} is more than {FPS_DROP_FAIL:.0%} below baseline {p[0]:.2f}")
    p = pair(b, c, "frame_ms_p99")
    if p and p[1] > p[0] * (1 + P99_RISE_FAIL):
        fail.append(f"frame_ms_p99 {p[1]:.2f} is more than 20% above baseline {p[0]:.2f}")
    for k in sorted(set(c["errors"]) - set(b["errors"])):
        fail.append(f"new error key in candidate: {k} (x{c['errors'][k]})")
    bp, cp = b.get("phases") or {}, c.get("phases") or {}
    for name in sorted(set(bp) - set(cp)):
        fail.append(f"phase {name} missing in candidate")
    for name in [n for n in bp if n in cp]:
        p = pair(bp[name], cp[name], "fps_mean")
        if p and p[1] < p[0] * (1 - FPS_DROP_FAIL):
            fail.append(f"phase {name} fps_mean {p[1]:.2f} is more than {FPS_DROP_FAIL:.0%} "
                        f"below baseline {p[0]:.2f}")
    return fail


def check_warn(b, c):
    warn = []
    bw, cw = b.get("wait_ms_per_s_median") or {}, c.get("wait_ms_per_s_median") or {}
    for k in sorted(set(bw) | set(cw)):
        p = pair(bw, cw, k)
        if p and p[1] - p[0] > WAIT_RISE_ABS and rel_change(*p) > WAIT_RISE_REL:
            warn.append(f"wait_ms_per_s_median.{k} rose {p[0]:.2f} -> {p[1]:.2f} ms/s")
    bc, cc = b.get("final_capture") or {}, c.get("final_capture") or {}
    p = pair(bc, cc, "luma_mean")
    if p and rel_change(*p) > LUMA_REL_WARN:
        warn.append(f"capture luma_mean differs {rel_change(*p):.0%} ({p[0]:.2f} -> {p[1]:.2f}); "
                    f"{CAPTURE_NOTE}")
    for k in ("black_pct", "saturated_pct"):
        p = pair(bc, cc, k)
        if p and abs(p[1] - p[0]) > BLACK_SAT_PP_WARN:
            warn.append(f"capture {k} differs by {abs(p[1] - p[0]):.2f} pp; {CAPTURE_NOTE}")
    gray = lambda d: d.get("clear_gray_frames") if isinstance(d.get("clear_gray_frames"), int) \
        else len(d.get("clear_gray_frames") or [])
    if gray(c) > gray(b):
        warn.append(f"captures showing the clear color (missing geometry) {gray(b)} -> {gray(c)}; "
                    f"{CAPTURE_NOTE}")
    bp, cp = b.get("phases") or {}, c.get("phases") or {}
    for name in [n for n in bp if n in cp]:
        p = pair(bp[name], cp[name], "frame_ms_p99")
        if p and p[1] > p[0] * (1 + P99_RISE_FAIL):
            warn.append(f"phase {name} frame_ms_p99 rose {p[0]:.2f} -> {p[1]:.2f} ms")
    l1 = hist_l1(bc.get("luma_hist16"), cc.get("luma_hist16"))
    if l1 is not None and l1 > HIST_L1_WARN:
        warn.append(f"capture luma_hist16 L1 distance {l1:.1f} pp (> {HIST_L1_WARN:g}); "
                    f"{CAPTURE_NOTE}")
    return warn


def print_table(rows):
    head = ["metric", "baseline", "candidate", "delta", "delta%"]
    body = [[r["metric"], fmt(r["baseline"]), fmt(r["candidate"]),
             "-" if r["delta"] is None else f"{r['delta']:+.2f}",
             "-" if r["delta_pct"] is None else f"{r['delta_pct']:+.1f}%"] for r in rows]
    widths = [max(len(line[i]) for line in [head] + body) for i in range(len(head))]
    for line in [head, ["-" * w for w in widths]] + body:
        print("  ".join(v.ljust(w) for v, w in zip(line, widths)).rstrip())


def print_list(title, items):
    print(f"{title} reasons:" + ("" if items else " none"))
    for item in items:
        print(f"  - {item}")


def main(argv):
    pos = [a for a in argv if not a.startswith("--")]
    flags = [a for a in argv if a.startswith("--")]
    if len(pos) != 2 or any(f != "--json" for f in flags):
        print("usage: compare.py <baseline> <candidate> [--json]", file=sys.stderr)
        return 2
    b = normalize(load(pos[0]))
    c = normalize(load(pos[1]))
    rows, fail, warn = build_rows(b, c), check_fail(b, c), check_warn(b, c)
    verdict = "FAIL" if fail else "WARN" if warn else "PASS"
    if "--json" in flags:
        print(json.dumps({"rows": rows, "fail": fail, "warn": warn, "verdict": verdict}, indent=2))
    else:
        print_table(rows)
        print_list("FAIL", fail)
        print_list("WARN", warn)
        print(f"VERDICT: {verdict}")
    return 1 if verdict == "FAIL" else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
