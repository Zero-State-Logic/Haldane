"""ctypes binding to the HALDANE portable core (firmware/core) built as a DLL.

Build it first with ``python tools/run_all.py`` (or ``make -C host``); the
library lands in ``build/``.
"""
from __future__ import annotations

import ctypes as C
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]
BUILD = REPO / "build"

IN_COLS = 13  # mean_q4[4], raw_min[4], raw_max[4], button
CH_MQ7, CH_MQ135, CH_LM35, CH_FLAME = 0, 1, 2, 3

PROFILE_UL2034, PROFILE_SENSITIVE = 0, 1
OCC_REST, OCC_LIGHT, OCC_MODERATE, OCC_HEAVY = 0, 1, 2, 3
OCC_NAMES = {OCC_REST: "rest", OCC_LIGHT: "light", OCC_MODERATE: "moderate", OCC_HEAVY: "heavy"}

KEY_CO_ALARM_X100, KEY_CO_WARN_X100, KEY_CO_OVERRIDE_PPM_Q4 = 0, 1, 2
KEY_OCC_PRESET, KEY_CALIBRATED = 3, 4

STATE_NAMES = ["NORMAL", "WARMUP", "TROUBLE", "CO_WARN", "CO_HUSH",
               "FIRE_HUSH", "TEST", "CO_ALARM", "FIRE_ALARM"]
STATE_CO_ALARM = 7
STATE_FIRE_ALARM = 8

SIZEOF_NAMES = ["hd_node_t", "hd_config_t", "hd_cfk_t", "hd_cfk_coeffs_t",
                "hd_mq7_t", "hd_fire_t", "hd_alarm_t", "hd_health_ch_t[4]", "hd_annun_t"]


def _lib_path() -> Path:
    if sys.platform.startswith("win"):
        name = "haldane_core.dll"
    elif sys.platform == "darwin":
        name = "libhaldane_core.dylib"
    else:
        name = "libhaldane_core.so"
    path = BUILD / name
    if not path.exists():
        raise FileNotFoundError(f"{path} not found - run `python tools/run_all.py` first")
    return path


def _load() -> C.CDLL:
    lib = C.CDLL(str(_lib_path()))
    i32, p_i32, p_u16, vp = C.c_int32, C.POINTER(C.c_int32), C.POINTER(C.c_uint16), C.c_void_p
    sig = {
        "hd_api_version": ([], i32),
        "hd_api_cfk_preset": ([i32, p_i32], i32),
        "hd_api_cfk_coeffs": ([p_i32, p_i32], i32),
        "hd_api_cfk_run": ([p_i32, p_u16, i32, i32, p_u16], i32),
        "hd_api_sizeof": ([i32], i32),
        "hd_api_config_default": ([vp, i32], i32),
        "hd_api_config_set": ([vp, i32, i32], i32),
        "hd_api_node_init": ([vp, vp], i32),
        "hd_api_out_cols": ([], i32),
        "hd_api_out_col_name": ([i32], C.c_char_p),
        "hd_api_node_run": ([vp, vp, i32, p_u16, p_u16, p_i32], i32),
    }
    for name, (args, res) in sig.items():
        fn = getattr(lib, name)
        fn.argtypes = args
        fn.restype = res
    return lib


LIB = _load()
OUT_COLS = LIB.hd_api_out_cols()
OUT = {LIB.hd_api_out_col_name(i).decode(): i for i in range(OUT_COLS)}


def _check(rc: int, what: str) -> None:
    if rc != 0:
        raise RuntimeError(f"{what} failed with status {rc}")


def preset(occ: int) -> list[int]:
    """Integer physiological parameters of an occupant preset."""
    out = (C.c_int32 * 8)()
    _check(LIB.hd_api_cfk_preset(occ, out), "hd_api_cfk_preset")
    return list(out)


def cfk_coeffs(physio: list[int]) -> list[int]:
    p = (C.c_int32 * 8)(*physio)
    out = (C.c_int32 * 3)()
    _check(LIB.hd_api_cfk_coeffs(p, out), "hd_api_cfk_coeffs")
    return list(out)


def cfk_run(physio: list[int], ppm: np.ndarray, init_pct: float | None = None) -> np.ndarray:
    """Fixed-point CFK, one step per element of ``ppm`` (seconds). Returns %COHb."""
    q4 = np.clip(np.round(np.asarray(ppm, dtype=np.float64) * 16.0), 0, 65535).astype(np.uint16)
    out = np.zeros(len(q4), dtype=np.uint16)
    p = (C.c_int32 * 8)(*physio)
    init = -1 if init_pct is None else int(round(init_pct * 100))
    rc = LIB.hd_api_cfk_run(p, q4.ctypes.data_as(C.POINTER(C.c_uint16)), len(q4), init,
                            out.ctypes.data_as(C.POINTER(C.c_uint16)))
    _check(rc, "hd_api_cfk_run")
    return out.astype(np.float64) / 100.0


def sizeof_report() -> dict[str, int]:
    return {name: LIB.hd_api_sizeof(i) for i, name in enumerate(SIZEOF_NAMES)}


class Node:
    """One simulated HALDANE node running the real core code."""

    def __init__(self, profile: int = PROFILE_UL2034, overrides: dict[int, int] | None = None):
        self._cfg = C.create_string_buffer(LIB.hd_api_sizeof(1))
        self._node = C.create_string_buffer(LIB.hd_api_sizeof(0))
        _check(LIB.hd_api_config_default(self._cfg, profile), "config_default")
        _check(LIB.hd_api_config_set(self._cfg, KEY_CALIBRATED, 1), "config_set")
        for key, value in (overrides or {}).items():
            _check(LIB.hd_api_config_set(self._cfg, key, int(value)), "config_set")
        _check(LIB.hd_api_node_init(self._node, self._cfg), "node_init")

    def run(self, inputs: np.ndarray, flame: np.ndarray | None = None) -> np.ndarray:
        """inputs: (n, 13) uint16. Returns (n, OUT_COLS) int32."""
        inputs = np.ascontiguousarray(inputs, dtype=np.uint16)
        if inputs.ndim != 2 or inputs.shape[1] != IN_COLS:
            raise ValueError(f"inputs must be (n, {IN_COLS})")
        n = inputs.shape[0]
        out = np.zeros((n, OUT_COLS), dtype=np.int32)
        fl = None
        if flame is not None:
            flame = np.ascontiguousarray(flame, dtype=np.uint16)
            if flame.shape != (n, 100):
                raise ValueError("flame must be (n, 100)")
            fl = flame.ctypes.data_as(C.POINTER(C.c_uint16))
        rc = LIB.hd_api_node_run(self._node, self._cfg, n,
                                 inputs.ctypes.data_as(C.POINTER(C.c_uint16)), fl,
                                 out.ctypes.data_as(C.POINTER(C.c_int32)))
        _check(rc, "hd_api_node_run")
        return out
