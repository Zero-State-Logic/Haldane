"""Synthetic sensor front end: true CO ppm per second -> the per-second ADC
statistics the node consumes (mean x16, raw min, raw max) on all 4 channels.

MQ-7 model (synthetic, not fitted to a real unit):
  * low (1.4 V) phase: log Rs relaxes toward the CO-dependent value
    Rs/R0 = (gain*ppm / 99.042)^(1/-1.518) (clamped at the clean-air 27.5)
    with a 20 s time constant;
  * high (5 V) phase: the hot sensor sits near Rs/R0 = 3 and partially
    forgets the CO reading;
  * ADC = 1024 * RL/(RL+Rs), RL = R0, small noise on the 1 s mean.
The heater schedule is the node's own: it boots into a 60 s high phase,
then 90 s low, repeating. ``gain`` models calibration error.
Other channels: LM35 at 22 degC, MQ-135 in clean air, dark flame sensor.
"""
from __future__ import annotations

import math

import numpy as np

from .core import IN_COLS, CH_MQ7, CH_MQ135, CH_LM35, CH_FLAME

HIGH_S, LOW_S = 60, 90
CYCLE_S = HIGH_S + LOW_S
TAU_S = 20.0


def heater_is_high(t: np.ndarray) -> np.ndarray:
    """Phase in force during second t (t = 0 is the first second after boot)."""
    return (np.asarray(t) % CYCLE_S) < HIGH_S


def mq7_ratio(ppm: np.ndarray, gain: float = 1.0) -> np.ndarray:
    eff = np.maximum(np.asarray(ppm, dtype=float) * gain, 0.5)
    return np.minimum((eff / 99.042) ** (1.0 / -1.518), 27.5)


def mq7_adc_mean(ppm: np.ndarray, gain: float = 1.0) -> np.ndarray:
    """Per-second mean MQ-7 ADC value (counts) under the heater schedule."""
    target_low = np.log(mq7_ratio(ppm, gain))
    high = heater_is_high(np.arange(len(target_low)))
    k = 1.0 - math.exp(-1.0 / TAU_S)
    log_hot = math.log(3.0)
    out = np.empty(len(target_low))
    state = target_low[0] if len(target_low) else 0.0
    tl = target_low.tolist()
    hl = high.tolist()
    for i in range(len(tl)):
        target = log_hot + 0.1 * (tl[i] - math.log(27.5)) if hl[i] else tl[i]
        state += k * (target - state)
        out[i] = state
    rs = np.exp(out)
    return 1024.0 / (1.0 + rs)


def _channel(mean_counts: np.ndarray, rng: np.random.Generator, noise: float) -> tuple:
    mean = np.clip(mean_counts + rng.normal(0.0, noise, len(mean_counts)), 0.0, 1023.0)
    raw = np.floor(mean)
    lo = np.clip(raw - 1, 0, 1023)
    hi = np.clip(raw + 2, 0, 1023)
    return np.round(mean * 16.0), lo, hi


def node_inputs(ppm: np.ndarray, gain: float = 1.0, seed: int = 0) -> np.ndarray:
    """(n, 13) uint16 node input matrix for a CO exposure series (1 value/s)."""
    rng = np.random.default_rng(seed)
    n = len(ppm)
    out = np.zeros((n, IN_COLS), dtype=np.uint16)
    channels = {
        CH_MQ7: mq7_adc_mean(ppm, gain),
        CH_MQ135: np.full(n, 1024.0 / (1.0 + 3.6)),
        CH_LM35: np.full(n, 22.0 * 10.0 * 1024.0 / 5000.0),
        CH_FLAME: np.full(n, 1000.0),
    }
    for ch, counts in channels.items():
        mean_q4, lo, hi = _channel(counts, rng, 0.05)
        out[:, ch] = mean_q4
        out[:, 4 + ch] = lo
        out[:, 8 + ch] = hi
    return out
