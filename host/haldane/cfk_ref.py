"""Floating-point reference for the Coburn-Forster-Kane (CFK) equation.

Non-linear form (O2Hb = Hbmax - COHb, as used by Peterson & Stewart 1975):

    d[COHb]/dt = VCO/Vb + PICO/(B*Vb) - [COHb]*PO2 / ([O2Hb]*M*B*Vb)
    B = 1/DL + PL/VA,   PL = PB - 47 mmHg,   PICO = ppm*1e-6*PL

Units: volumes mL (STPD), pressures mmHg, time minutes, DL mL/min/mmHg,
VA mL/min, Hb g/dL, Hbmax = 1.38 mL CO per g Hb. Parameters come in the same
integer form the firmware uses (see firmware/core/hd_cfk.h).
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np


@dataclass(frozen=True)
class Physio:
    vb_ml: float          # blood volume
    va_ml_min: float      # alveolar ventilation
    dl: float             # diffusing capacity for CO, mL/min/mmHg
    hb_g_dl: float        # haemoglobin
    haldane_m: float      # Haldane constant
    po2_mmhg: float       # mean pulmonary capillary pO2
    vco_ml_min: float     # endogenous CO production
    pb_mmhg: float        # barometric pressure

    @classmethod
    def from_ints(cls, v: list[int]) -> "Physio":
        return cls(v[0], v[1], v[2] / 10.0, v[3] / 10.0, v[4], v[5], v[6] / 1000.0, v[7])

    def rates(self, pico_uses_pb: bool = False) -> tuple[float, float, float]:
        """(a, b, c) for dx/dt = a + b*ppm - c*x/(1-x), x = COHb/Hbmax, per minute."""
        hbmax = 1.38 * self.hb_g_dl / 100.0
        pl = self.pb_mmhg - 47.0
        bb = 1.0 / self.dl + pl / self.va_ml_min
        p_per_ppm = 1e-6 * (self.pb_mmhg if pico_uses_pb else pl)
        a = self.vco_ml_min / (self.vb_ml * hbmax)
        b = p_per_ppm / (bb * self.vb_ml * hbmax)
        c = self.po2_mmhg / (self.haldane_m * bb * self.vb_ml * hbmax)
        return a, b, c


def simulate(p: Physio, ppm_per_s: np.ndarray, x0: float | None = None,
             pico_uses_pb: bool = False) -> np.ndarray:
    """RK4 at 1 s steps; ppm held constant within each second. Returns %COHb."""
    a, b, c = p.rates(pico_uses_pb)
    x = a / (a + c) if x0 is None else x0
    dt = 1.0 / 60.0
    out = np.empty(len(ppm_per_s))

    def f(xv: float, ppm: float) -> float:
        return a + b * ppm - c * xv / (1.0 - xv)

    for i, ppm in enumerate(np.asarray(ppm_per_s, dtype=float)):
        k1 = f(x, ppm)
        k2 = f(x + 0.5 * dt * k1, ppm)
        k3 = f(x + 0.5 * dt * k2, ppm)
        k4 = f(x + dt * k3, ppm)
        x += dt / 6.0 * (k1 + 2 * k2 + 2 * k3 + k4)
        out[i] = 100.0 * x
    return out


def equilibrium_pct(p: Physio, ppm: float, pico_uses_pb: bool = False) -> float:
    a, b, c = p.rates(pico_uses_pb)
    r = (a + b * ppm) / c
    return 100.0 * r / (1.0 + r)
