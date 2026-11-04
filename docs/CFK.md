# The CFK model in HALDANE

## Equation

Coburn, Forster & Kane (1965), in the non-linear form used by Peterson & Stewart (1975), where oxyhaemoglobin is whatever haemoglobin is not carrying CO:

```
d[COHb]/dt = VCO/Vb + PICO/(B*Vb) - [COHb]*PO2 / ([O2Hb]*M*B*Vb)
B          = 1/DL + PL/VA        PL = PB - 47 mmHg
[O2Hb]     = Hbmax - [COHb]      Hbmax = 1.38 mL CO per g Hb * Hb
PICO       = ppm * 1e-6 * PL     (CO in fully humidified inspired gas)
```

HALDANE integrates the saturation `x = [COHb]/Hbmax`, so `dx/dt = a + b*ppm - c*x/(1-x)` with `a`, `b`, `c` derived once at start-up from the parameters below (`firmware/core/hd_cfk.c` has the integer algebra).

## Parameters

| Symbol | Meaning | Value | Units |
| --- | --- | --- | --- |
| Vb | blood volume | 5500 | mL |
| Hb | haemoglobin | 15.0 | g/dL |
| Hbmax | CO capacity (1.38 x Hb) | 0.207 | mL CO / mL blood |
| M | Haldane constant (CO vs O2 affinity) | 218 | - |
| PO2 | mean pulmonary capillary O2 pressure | 100 | mmHg |
| VCO | endogenous CO production | 0.007 | mL/min STPD |
| PB | barometric pressure | 760 | mmHg |
| PH2O | water vapour pressure at 37 degC | 47 | mmHg |
| VA, DL | alveolar ventilation, CO diffusing capacity | see presets | mL/min, mL/min/mmHg |

These are the standard-adult values commonly used with the CFK equation. The activity presets are illustrative, not taken from a specific table:

| Preset | VA (mL/min) | DL (mL/min/mmHg) |
| --- | --- | --- |
| rest | 6 000 | 30 |
| light | 10 000 | 35 |
| moderate | 20 000 | 40 |
| heavy (default reference occupant) | 30 000 | 45 |

## Fixed point

* State: `x` in Q30 (int32). Coefficients: `b*dt` per 1/16 ppm in Q46, `a*dt` in Q42, `c*dt` in Q36.
* One step per second, forward Euler. Every time constant is longer than 30 minutes, so the step error is negligible.
* `x/(1-x)` uses a 32-bit shift-subtract divider. Products use a 32x32->64 multiply and a rounding shift. The per-second path has no 64-bit division and no floating point. Coefficient derivation runs once at boot and does use 64-bit integer division.
* Accuracy against a double-precision RK4 reference: worst 0.0017 %COHb over 8 h, across 4 presets x 4 exposures (`host/ctest/test_cfk.c`). Through the DLL against the Python reference, including the 0.01 % readout step, the worst is 0.0067 % (`results/co_eval.json`).

## Why the alarm is at 5.5 %, not 10 %

UL 2034 alarm windows, as I understand them from manufacturer documentation (I have not read the paywalled standard): 70 ppm within 60-240 min, 150 ppm within 10-50 min, 400 ppm within 4-15 min, and no alarm at 30 ppm for 30 days. Manufacturer manuals describe these points as tied to roughly 10 % COHb.

With the parameters above, the CFK model does not support a single 10 % threshold:

* At 70 ppm the steady state is 9.87-10.00 % COHb depending on activity (10.45-10.58 % if PICO is computed with PB instead of PB-47). 10 % is either never reached or reached long after 240 min.
* The COHb thresholds that satisfy all three windows (ideal, instant sensing) form a band that moves with activity: rest 1.83-2.65 %, light 2.49-3.80 %, moderate 3.65-5.98 %, heavy 4.48-7.70 %.

So HALDANE models the worst-case-uptake occupant (heavy exertion) and alarms at 5.5 %, near the middle of that band once the MQ-7's 150 s sampling delay is included. At that moment a resting adult in the same room is lower still. The 10 % line stays in the design as the level above which the CO alarm refuses to be silenced. A commercial COHb-estimating detector patent (US 7,408,473) also uses thresholds below 10 % (6.5-9 %, depending on concentration).

## References

* R. F. Coburn, R. E. Forster, P. B. Kane, "Considerations of the physiological variables that determine the blood carboxyhemoglobin concentration in man," J. Clin. Invest. 44(11):1899-1910, 1965.
* J. E. Peterson, R. D. Stewart, "Predicting the carboxyhemoglobin levels resulting from carbon monoxide exposures," J. Appl. Physiol. 39(4):633-638, 1975.
* C. G. Douglas, J. S. Haldane, J. B. S. Haldane, "The laws of combination of haemoglobin with carbon monoxide and oxygen," J. Physiol. 44(4):275-304, 1912.
* UL 2034, Standard for Single and Multiple Station Carbon Monoxide Alarms. EN 50291-1, Electrical apparatus for the detection of carbon monoxide in domestic premises.
