"""Casos de JSON como Python (json.loads, json.dumps y repr de float) para export_fixtures.py.

  floats   repr(x) de dobles: bits aleatorios, decimales redondeados y los bordes de la
           notación científica (1e-05, 1e+16) → [bits en hex, repr(x)]
  dumps    json.dumps(valor, ensure_ascii=False, indent=n) → [valor, n, texto]
  loads    json.loads(texto) → {"text", "result"} o {"text", "error"}

No necesita video-yt: es el json y el float de CPython.
"""
import math
import random
import struct

_rng = random.Random(20261008)


def _from_bits(bits: int) -> float:
    return struct.unpack("<d", struct.pack("<Q", bits))[0]


def floats() -> list[float]:
    out = [0.1, 0.2, 0.3, 1 / 3, 2 / 3, 1.0, 100.0, 123.456, 279.03900000000567, 279.0390000000057,
           1e15, 1e15 + 0.5, 1234567890123456.0, 9999999999999998.0, 1e16, 1.5e16, 1e17, 1e22, 1e23, 1e300,
           1.7976931348623157e308, 5e-324, 2.2250738585072014e-308, 1e-4, 0.00012, 1e-5, 0.000099, 1e-7,
           -0.0, -1.5, -1e-5, -1e16, float(2 ** 53), float(2 ** 53) + 2.0, 0.1 + 0.2, 1 - 1e-16, 4.35, 0.07]
    for _ in range(3000):  # bits aleatorios (sin NaN ni infinitos)
        x = _from_bits(_rng.getrandbits(64))
        if math.isfinite(x):
            out.append(x)
    for _ in range(2000):  # decimales redondeados, como los de las timelines
        out.append(round(_rng.uniform(-1e4, 1e4), _rng.randint(0, 6)))
    for e in range(-10, 25):  # alrededor de cada potencia de 10
        for m in (1.0, 1.5, 9.999999999999998, 9.5):
            out.append(m * 10.0 ** e)
    return out


FLOATS = [[f"{struct.unpack('<Q', struct.pack('<d', x))[0]:016x}", repr(x)] for x in floats()]

VALUE = {
    "texto": "comillas \" barra \\ / tab\t nueva\nlínea\r \b \f \x00 \x01 \x1f \x7f ñ 🎬   ﻿",
    "números": [0, -1, 18446744073709551615, -9223372036854775808, 1.0, -0.0, 0.1, 1e16, 1e-05,
                1234567890123456.0, 279.03900000000567, float("nan"), float("inf"), float("-inf")],
    "vacíos": {"lista": [], "dict": {}, "texto": ""},
    "anidado": [[[]], [{}], {"a": [1, {"b": None}]}],
    "bool": [True, False, None],
}

DUMPS = [[VALUE, n] for n in (None, 0, 2, 4)] + [
    [[], None], [{}, 2], ["solo un texto", 2], [1.5, None], [[1, [2, [3]]], 2], [{"a": {"b": {}}}, 2],
]

LOADS = [
    '{"projects": [], "x": NaN, "y": Infinity, "z": -Infinity}',
    '[1e999, -1e999, 1e-999, -1e-999, 1E400, 0.0e999, 0e-5]',
    '{"NaN": "Infinity", "a": "-Infinity NaN 1e999", "b": ["\\"NaN", NaN]}',
    '{"k": "\\\\", "NaN": NaN}',
    '[1.5e308, 2e308, 123456789e300, 0.000001e310, 100000000000000000000]',
    '[18446744073709551615, -9223372036854775808, 9223372036854775807]',
    '{"a": 1, "a": 2, "b": NaN, "b": 3}',
    '  [ NaN , Infinity ]  ',
    # Errores de json.loads.
    '-NaN', 'nan', 'inf', '[1NaN]', '[NaNx]', 'Infinityx', '[--1]', '{"a": +1}', '[1.]', '[.5]', '01',
]
