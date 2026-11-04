"""Python tests: the C core through ctypes, against the float reference and
the CO-alarm behaviour claimed in the README. Run from host/:

    python -m unittest discover -s tests -v
"""
from __future__ import annotations

import unittest

import numpy as np

from haldane import cfk_ref, core, eval_co, sensors
from haldane.core import OUT, STATE_CO_ALARM


class CfkAgainstFloat(unittest.TestCase):
    def test_coefficients_match_float_rates(self):
        for occ in core.OCC_NAMES:
            phys = core.preset(occ)
            k_ppm, k_endo, k_elim = core.cfk_coeffs(phys)
            a, b, c = cfk_ref.Physio.from_ints(phys).rates()
            self.assertAlmostEqual(k_ppm / 2**46 * 960 / b, 1.0, places=4)
            self.assertAlmostEqual(k_endo / 2**42 * 60 / a, 1.0, places=4)
            self.assertAlmostEqual(k_elim / 2**36 * 60 / c, 1.0, places=4)

    def test_fixed_point_tracks_float_8h(self):
        t = np.arange(8 * 3600)
        exposures = [np.full(len(t), 150.0), np.where((t % 3600) < 1200, 180.0, 10.0)]
        for occ in (core.OCC_REST, core.OCC_HEAVY):
            phys = core.preset(occ)
            ref = cfk_ref.Physio.from_ints(phys)
            for ppm in exposures:
                err = np.abs(core.cfk_run(phys, ppm) - cfk_ref.simulate(ref, ppm)).max()
                self.assertLess(err, 0.1)

    def test_ten_percent_is_outside_the_ul_band(self):
        band = eval_co.threshold_band()
        lo, hi = band["heavy"]["feasible_threshold_pct"]
        self.assertTrue(lo < eval_co.ALARM_PCT < hi)
        for row in band.values():
            self.assertLess(row["feasible_threshold_pct"][1], 10.0)
            self.assertIsNone(row["minutes_to_10pct"]["70"])


class NodeBehaviour(unittest.TestCase):
    def test_heater_schedule_matches_sensor_model(self):
        out = core.Node().run(sensors.node_inputs(np.zeros(600)))
        # row i reports the phase for second i+1 (1 = low phase)
        expected_low = ~sensors.heater_is_high(np.arange(1, 601))
        np.testing.assert_array_equal(out[:, OUT["mq7_phase"]] == 1, expected_low)

    def test_ul2034_windows_through_full_chain(self):
        for level, window in eval_co.UL.items():
            res = eval_co.step_times(level, window[1] + 30, 3, core.PROFILE_UL2034, seed0=1)
            for t in res["haldane_min"]:
                self.assertIsNotNone(t)
                self.assertGreaterEqual(t, window[0])
                self.assertLessEqual(t, window[1])

    def test_cooking_spike_alarms_baseline_not_haldane(self):
        ppm, start = eval_co.exposure(lambda t: np.where(t < 300, 200.0, 0.0), 90, 40)
        out = eval_co.run_node(ppm)
        self.assertFalse((out[start:, OUT["state"]] == STATE_CO_ALARM).any())
        base = eval_co.baselines(out[:, OUT["co_ppm_q4"]] / 16.0)
        self.assertTrue(base["instant_50ppm"][start:].any())

    def test_sensitive_profile_override(self):
        ppm, start = eval_co.exposure(lambda t: np.full(len(t), 320.0), 10, 0)
        out = eval_co.run_node(ppm, core.PROFILE_SENSITIVE)
        t = eval_co.first_minutes(out[:, OUT["state"]] == STATE_CO_ALARM, start)
        self.assertIsNotNone(t)
        self.assertLess(t, 3.5)

    def test_bad_input_shape_rejected(self):
        with self.assertRaises(ValueError):
            core.Node().run(np.zeros((10, 5), dtype=np.uint16))

    def test_footprint_report(self):
        sizes = core.sizeof_report()
        self.assertEqual(sizes["hd_cfk_t"], 4)
        self.assertLess(sizes["hd_node_t"], 400)


if __name__ == "__main__":
    unittest.main()
