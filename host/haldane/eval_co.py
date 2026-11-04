"""CO alarm evaluation: runs the real core (via ctypes) on simulated exposures.

    python -m haldane.eval_co            (from host/)

Writes results/co_eval.json and docs/assets/cohb_ul_windows.svg. Everything
here is simulated: synthetic MQ-7 front end, no hardware.
"""
from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np

from . import cfk_ref, core, sensors
from .core import Node, OUT, PROFILE_SENSITIVE, PROFILE_UL2034, STATE_CO_ALARM

UL = {70: (60.0, 240.0), 150: (10.0, 50.0), 400: (4.0, 15.0)}
EN50291 = {50: (60.0, 90.0), 100: (10.0, 40.0), 300: (0.0, 3.0)}
ALARM_PCT = 5.5
WARMUP_S = 600


def exposure(ppm_fn, minutes: float, offset_s: int) -> tuple[np.ndarray, int]:
    """Clean-air warm-up of WARMUP_S + offset_s seconds, then the exposure."""
    start = WARMUP_S + offset_s
    t = np.arange(int(minutes * 60))
    ppm = np.concatenate([np.zeros(start), ppm_fn(t)])
    return ppm, start


def first_minutes(mask: np.ndarray, start: int) -> float | None:
    idx = np.flatnonzero(mask[start:])
    return None if idx.size == 0 else (idx[0] + 1) / 60.0


def baselines(measured_ppm: np.ndarray) -> dict[str, np.ndarray]:
    kernel = np.ones(900) / 900.0
    ma15 = np.convolve(measured_ppm, kernel)[: len(measured_ppm)]
    return {"instant_50ppm": measured_ppm >= 50.0, "ma15_50ppm": ma15 >= 50.0}


def run_node(ppm: np.ndarray, profile: int = PROFILE_UL2034, gain: float = 1.0,
             seed: int = 0) -> np.ndarray:
    return Node(profile).run(sensors.node_inputs(ppm, gain=gain, seed=seed))


def step_times(level: float, minutes: float, reps: int, profile: int, gain: float = 1.0,
               seed0: int = 0) -> dict:
    """Alarm times for a CO step, exposure starting at evenly spread heater phases."""
    haldane, base = [], {"instant_50ppm": [], "ma15_50ppm": []}
    curve = None
    for r in range(reps):
        offset = int(round(r * sensors.CYCLE_S / reps))
        ppm, start = exposure(lambda t: np.full(len(t), float(level)), minutes, offset)
        out = run_node(ppm, profile, gain, seed0 + r)
        haldane.append(first_minutes(out[:, OUT["state"]] == STATE_CO_ALARM, start))
        meas = out[:, OUT["co_ppm_q4"]] / 16.0
        for name, mask in baselines(meas).items():
            base[name].append(first_minutes(mask, start))
        if r == reps // 2:
            curve = out[start:, OUT["cohb_x100"]] / 100.0
    return {"haldane_min": haldane, "baseline_min": base, "curve": curve}


def summarize(times: list, window: tuple[float, float]) -> dict:
    valid = [t for t in times if t is not None]
    inside = [t is not None and window[0] <= t <= window[1] for t in times]
    return {
        "n": len(times),
        "min": round(min(valid), 2) if valid else None,
        "median": round(float(np.median(valid)), 2) if valid else None,
        "max": round(max(valid), 2) if valid else None,
        "never": len(times) - len(valid),
        "inside_window": int(sum(inside)),
        "all_inside": bool(all(inside)),
    }


def ul_windows(reps: int = 30) -> tuple[dict, dict]:
    table, curves = {}, {}
    for level, win in UL.items():
        res = step_times(level, win[1] + 60, reps, PROFILE_UL2034, seed0=level)
        table[str(level)] = {
            "window_min": list(win),
            "haldane": summarize(res["haldane_min"], win),
            **{k: summarize(v, win) for k, v in res["baseline_min"].items()},
        }
        curves[level] = (res["curve"], res["haldane_min"])
    return table, curves


def long_exposures() -> dict:
    out = {}
    ppm, start = exposure(lambda t: np.full(len(t), 30.0), 30 * 24 * 60, 0)
    res = run_node(ppm)
    out["30ppm_30days"] = {
        "peak_cohb_pct": float(res[start:, OUT["cohb_x100"]].max() / 100.0),
        "haldane_alarm": bool((res[start:, OUT["state"]] == STATE_CO_ALARM).any()),
        **{k: bool(v[start:].any()) for k, v in baselines(res[:, OUT["co_ppm_q4"]] / 16.0).items()},
    }
    spikes = []
    for r in range(10):
        ppm, start = exposure(lambda t: np.where(t < 300, 200.0, 0.0), 120, r * 15)
        res = run_node(ppm, seed=100 + r)
        meas = res[:, OUT["co_ppm_q4"]] / 16.0
        spikes.append({
            "peak_cohb_pct": float(res[start:, OUT["cohb_x100"]].max() / 100.0),
            "haldane_alarm": bool((res[start:, OUT["state"]] == STATE_CO_ALARM).any()),
            **{k: bool(v[start:].any()) for k, v in baselines(meas).items()},
        })
    out["spike_200ppm_5min"] = {
        "runs": len(spikes),
        "peak_cohb_pct_max": max(s["peak_cohb_pct"] for s in spikes),
        "haldane_alarms": sum(s["haldane_alarm"] for s in spikes),
        "instant_50ppm_alarms": sum(s["instant_50ppm"] for s in spikes),
        "ma15_50ppm_alarms": sum(s["ma15_50ppm"] for s in spikes),
    }
    return out


def kitchen_week(seed: int, peak_range: tuple[float, float]) -> np.ndarray:
    """7 days: 1-3 ppm background plus 3 gas-cooking events a day."""
    rng = np.random.default_rng(seed)
    n = 7 * 86400
    ppm = np.full(n, rng.uniform(1.0, 3.0))
    t = np.arange(n)
    for day in range(7):
        for hour in (8.0, 13.0, 19.5):
            t0 = int((day * 24 + hour + rng.uniform(-0.5, 0.5)) * 3600)
            peak, dur = rng.uniform(*peak_range), int(rng.uniform(3.0, 12.0) * 60)
            seg = t[t0:t0 + dur + 3600] - t0
            shape = np.where(seg < dur, peak * (1 - np.exp(-seg / 60.0)),
                             peak * (1 - math.exp(-dur / 60.0)) * np.exp(-(seg - dur) / 600.0))
            ppm[t0:t0 + len(seg)] += shape
    return ppm


def count_episodes(mask: np.ndarray) -> int:
    m = mask.astype(np.int8)
    return int(np.count_nonzero(np.diff(m) == 1) + m[0])


def nuisance_weeks(peak_range: tuple[float, float], weeks: int = 4) -> dict:
    counts = {"haldane": 0, "instant_50ppm": 0, "ma15_50ppm": 0}
    peak = 0.0
    for w in range(weeks):
        ppm = np.concatenate([np.zeros(WARMUP_S), kitchen_week(500 + w, peak_range)])
        res = run_node(ppm, seed=500 + w)
        counts["haldane"] += count_episodes(res[WARMUP_S:, OUT["state"]] == STATE_CO_ALARM)
        for k, v in baselines(res[:, OUT["co_ppm_q4"]] / 16.0).items():
            counts[k] += count_episodes(v[WARMUP_S:])
        peak = max(peak, float(res[WARMUP_S:, OUT["cohb_x100"]].max() / 100.0))
    return {"weeks": weeks, "cooking_events": weeks * 21, "event_peak_ppm": list(peak_range),
            "event_minutes": [3, 12], "decay_tau_min": 10, "alarm_episodes": counts,
            "peak_cohb_pct": peak}


def danger_scenarios() -> dict:
    cases = {
        "slow_leak_0_to_150ppm_over_2h": lambda t: np.minimum(t / 7200.0, 1.0) * 150.0,
        "step_600ppm": lambda t: np.full(len(t), 600.0),
        "furnace_cycling_250_20ppm": lambda t: np.where((t // 1200) % 2 == 0, 250.0, 20.0),
    }
    out = {}
    for name, fn in cases.items():
        ppm, start = exposure(fn, 360, 37)
        res = run_node(ppm, seed=7)
        meas = res[:, OUT["co_ppm_q4"]] / 16.0
        t_h = first_minutes(res[:, OUT["state"]] == STATE_CO_ALARM, start)
        out[name] = {"haldane_min": None if t_h is None else round(t_h, 1),
                     **{k: (None if (m := first_minutes(v, start)) is None else round(m, 1))
                        for k, v in baselines(meas).items()}}
    return out


def threshold_band() -> dict:
    """Ideal sensing, CFK only: the COHb thresholds that satisfy all UL windows."""
    out = {}
    for occ, name in core.OCC_NAMES.items():
        phys = core.preset(occ)
        lo, hi, t10 = 0.0, 100.0, {}
        for level, (w0, w1) in UL.items():
            curve = core.cfk_run(phys, np.full(int(w1 * 60) + 3 * 3600, float(level)))
            lo = max(lo, curve[int(w0 * 60) - 1])
            hi = min(hi, curve[int(w1 * 60) - 1])
            hit = np.flatnonzero(curve >= 10.0)
            t10[str(level)] = None if hit.size == 0 else round((hit[0] + 1) / 60.0, 1)
        p = cfk_ref.Physio.from_ints(phys)
        out[name] = {
            "va_ml_min": phys[1], "dl": phys[2] / 10.0,
            "feasible_threshold_pct": [round(lo, 2), round(hi, 2)],
            "steady_state_70ppm_pct": round(cfk_ref.equilibrium_pct(p, 70.0), 2),
            "steady_state_70ppm_pct_if_pico_uses_PB": round(cfk_ref.equilibrium_pct(p, 70.0, True), 2),
            "steady_state_30ppm_pct": round(cfk_ref.equilibrium_pct(p, 30.0), 2),
            "minutes_to_10pct": t10,
        }
    return out


def gain_robustness() -> dict:
    out = {}
    heavy = core.preset(core.OCC_HEAVY)
    for gain in (0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.3):
        ok = True
        detail = {}
        for level, win in UL.items():
            res = step_times(level, win[1] + 60, 6, PROFILE_UL2034, gain, seed0=900)
            s = summarize(res["haldane_min"], win)
            detail[str(level)] = [s["min"], s["max"]]
            ok = ok and s["all_inside"]
        ss30 = cfk_ref.equilibrium_pct(cfk_ref.Physio.from_ints(heavy), 30.0 * gain)
        out[str(gain)] = {"ul_windows_pass": ok, "alarm_min_range": detail,
                          "steady_state_30ppm_pct": round(ss30, 2),
                          "passes_30ppm_30days": ss30 < ALARM_PCT}
    return out


def sensitive_profile(reps: int = 10) -> dict:
    out = {}
    for level, win in EN50291.items():
        res = step_times(level, max(win[1] + 30, 60), reps, PROFILE_SENSITIVE, seed0=300 + level)
        out[str(level)] = {"window_min": list(win), **summarize(res["haldane_min"], win)}
    res = step_times(30, 125, reps, PROFILE_SENSITIVE, seed0=330)
    early = [t for t in res["haldane_min"] if t is not None and t < 120.0]
    out["30"] = {"no_alarm_before_120min": len(early) == 0, "alarms_before_120min": len(early)}
    return out


def fixed_vs_float() -> dict:
    t = np.arange(8 * 3600)
    cases = {
        "70ppm": np.full(len(t), 70.0),
        "400ppm": np.full(len(t), 400.0),
        "square_180_10ppm": np.where((t % 3600) < 1200, 180.0, 10.0),
        "ramp_0_300_0ppm": np.where(t < 4 * 3600, t / 3600.0 * 75.0, (8 - t / 3600.0) * 75.0),
    }
    worst = 0.0
    for occ in core.OCC_NAMES:
        phys = core.preset(occ)
        p = cfk_ref.Physio.from_ints(phys)
        for ppm in cases.values():
            q = np.round(ppm * 16.0) / 16.0
            worst = max(worst, float(np.abs(core.cfk_run(phys, q) - cfk_ref.simulate(p, q)).max()))
    return {"hours": 8, "presets": len(core.OCC_NAMES), "exposures": list(cases),
            "max_abs_error_pct_cohb": round(worst, 4),
            "note": "includes the 0.01 % output quantisation of the fixed-point readout"}


def chart(curves: dict, path: Path) -> None:
    """COHb vs time (log axis) for the three UL levels, windows shaded."""
    w, h, l, r, t, b = 760, 360, 64, 24, 28, 52
    x0, x1 = math.log10(1.0), math.log10(300.0)
    ymax = 12.0

    def px(m: float) -> float:
        return l + (math.log10(max(m, 1.0)) - x0) / (x1 - x0) * (w - l - r)

    def py(v: float) -> float:
        return t + (1 - v / ymax) * (h - t - b)

    colors = {70: "#0C8F9B", 150: "#E9B000", 400: "#7A4FD6"}
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}" '
         'font-family="Segoe UI,Helvetica,Arial,sans-serif" font-size="12">',
         '<title>Estimated COHb at the UL 2034 test levels</title>',
         f'<rect width="{w}" height="{h}" fill="#FBFBF8"/>']
    for v in range(0, 13, 2):
        s.append(f'<line x1="{l}" x2="{w - r}" y1="{py(v):.1f}" y2="{py(v):.1f}" stroke="#E4E6E8"/>')
        s.append(f'<text x="{l - 8}" y="{py(v) + 4:.1f}" text-anchor="end" fill="#555">{v}</text>')
    for m in (1, 2, 5, 10, 20, 50, 100, 200):
        s.append(f'<line x1="{px(m):.1f}" x2="{px(m):.1f}" y1="{t}" y2="{h - b}" stroke="#EEF0F2"/>')
        s.append(f'<text x="{px(m):.1f}" y="{h - b + 18}" text-anchor="middle" fill="#555">{m}</text>')
    for i, (level, (lo, hi)) in enumerate(UL.items()):
        y = py(ALARM_PCT) - 9 + i * 6
        s.append(f'<rect x="{px(lo):.1f}" y="{y:.1f}" width="{px(hi) - px(lo):.1f}" height="5" rx="2" '
                 f'fill="{colors[level]}" fill-opacity="0.35"/>')
    s.append(f'<line x1="{l}" x2="{w - r}" y1="{py(10):.1f}" y2="{py(10):.1f}" stroke="#D62828" '
             'stroke-width="2"/>')
    s.append(f'<text x="{l + 6}" y="{py(10) - 6:.1f}" fill="#D62828">10 % COHb</text>')
    s.append(f'<line x1="{l}" x2="{w - r}" y1="{py(ALARM_PCT):.1f}" y2="{py(ALARM_PCT):.1f}" '
             'stroke="#14171A" stroke-dasharray="5 4"/>')
    s.append(f'<text x="{l + 6}" y="{py(ALARM_PCT) + 22:.1f}" fill="#14171A">alarm 5.5 % '
             '(bars: UL 2034 windows)</text>')
    for level, (curve, times) in curves.items():
        mins = (np.arange(len(curve)) + 1) / 60.0
        keep = (mins >= 1.0) & (mins <= 300.0)
        pts = " ".join(f"{px(m):.1f},{py(v):.1f}" for m, v in zip(mins[keep][::20], curve[keep][::20]))
        s.append(f'<polyline points="{pts}" fill="none" stroke="{colors[level]}" stroke-width="2.4"/>')
        valid = [x for x in times if x is not None]
        if valid:
            tm = float(np.median(valid))
            s.append(f'<circle cx="{px(tm):.1f}" cy="{py(ALARM_PCT):.1f}" r="4.5" fill="{colors[level]}" '
                     'stroke="#fff" stroke-width="1.5"/>')
        lx = px(min(mins[keep][-1], 280.0))
        s.append(f'<text x="{lx - 4:.1f}" y="{py(curve[keep][-1]) - 6:.1f}" text-anchor="end" '
                 f'fill="{colors[level]}" font-weight="700">{level} ppm</text>')
    s.append(f'<text x="{(l + w - r) / 2}" y="{h - 12}" text-anchor="middle" fill="#333">minutes of '
             'exposure (log scale) - simulated MQ-7 front end, fixed-point core</text>')
    s.append(f'<text x="16" y="{(t + h - b) / 2}" transform="rotate(-90 16 {(t + h - b) / 2})" '
             'text-anchor="middle" fill="#333">estimated COHb, %</text>')
    s.append("</svg>")
    path.write_text("\n".join(s), encoding="utf-8")


def main() -> None:
    results_dir = core.REPO / "results"
    assets = core.REPO / "docs" / "assets"
    results_dir.mkdir(exist_ok=True)
    assets.mkdir(parents=True, exist_ok=True)
    ul, curves = ul_windows()
    results = {
        "simulated": True,
        "note": "Synthetic sensor front end; no hardware measurements.",
        "profile": {"name": "UL2034", "occupant": "heavy exertion (VA 30 L/min, DL 45)",
                    "alarm_cohb_pct": ALARM_PCT, "prealarm_fan_pct": 3.0,
                    "sensitive_alarm_cohb_pct": 3.3, "sensitive_override_ppm": 250},
        "ul2034_windows": ul,
        "threshold_band_ideal_sensing": threshold_band(),
        "long_exposures": long_exposures(),
        "kitchen_weeks_typical": nuisance_weeks((50.0, 150.0)),
        "kitchen_weeks_heavy": nuisance_weeks((100.0, 300.0)),
        "danger_scenarios": danger_scenarios(),
        "gain_robustness": gain_robustness(),
        "sensitive_profile_en50291": sensitive_profile(),
        "fixed_vs_float": fixed_vs_float(),
        "sizeof_host_bytes": core.sizeof_report(),
    }
    (results_dir / "co_eval.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    chart(curves, assets / "cohb_ul_windows.svg")
    print(json.dumps({"ul2034_windows": ul, "long": results["long_exposures"],
                      "kitchen_typical": results["kitchen_weeks_typical"],
                      "kitchen_heavy": results["kitchen_weeks_heavy"],
                      "sensitive": results["sensitive_profile_en50291"]}, indent=1))


if __name__ == "__main__":
    main()
