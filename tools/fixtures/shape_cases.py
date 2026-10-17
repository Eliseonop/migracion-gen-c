"""Casos de la geometría de las figuras (shapes.py): tipos con sus variantes, trazados, recortes,
guiones, volteos y «dibujar trazo» con y sin keyframes. Los usa synthetic_shape_fixtures."""

_TYPES = ["rect", "square", "circle", "ellipse", "line", "arrow", "arrow_double", "arrow_curve", "triangle",
          "star", "polygon", "speech", "check", "x", "marker", "heart", "letterbox", "path", "blob", ""]
_TRI = [[0, 100], [50, 0], [100, 100]]
_ZIG = [[0, 50], [25, 0], [50, 100], [75, 0], [100, 50]]

GEOMETRY = [(t, {}) for t in _TYPES] + [
    # Radio de las esquinas (en 0–0,5 de la caja; por debajo de 0,4 unidades, esquinas vivas).
    ("rect", {"cornerRadius": 0.12}), ("rect", {"cornerRadius": 0.5}), ("rect", {"cornerRadius": 0.003}),
    ("square", {"cornerRadius": 0.3}), ("rect", {"cornerRadius": "0.2"}), ("rect", {"cornerRadius": 0}),
    ("speech", {"cornerRadius": 0.05}), ("speech", {"cornerRadius": 0.4}),
    # Lados de estrellas y polígonos: round() al par y acotados a 3–12.
    ("star", {"sides": 3}), ("star", {"sides": 7.5}), ("star", {"sides": 6.5}), ("star", {"sides": 20}),
    ("polygon", {"sides": 3}), ("polygon", {"sides": 12}), ("polygon", {"sides": "4"}), ("polygon", {"sides": 0}),
    # Puntas de flecha: rellena, solo el contorno y sin punta.
    ("arrow", {"arrowHead": "line"}), ("arrow", {"arrowHead": "none"}), ("arrow_double", {"arrowHead": "line"}),
    ("arrow_curve", {"arrowHead": "none"}), ("arrow_curve", {"arrowHead": "filled"}), ("rect", {"arrowHead": "line"}),
    # Barras de cine.
    ("letterbox", {"bar": 0}), ("letterbox", {"bar": 0.3}), ("letterbox", {"bar": None}), ("letterbox", {"bar": 0.7}),
    # Trazados.
    ("path", {"points": _TRI, "closed": False, "smooth": True}),
    ("path", {"points": _TRI, "closed": False, "smooth": False}),
    ("path", {"points": _ZIG, "closed": True}),
    ("path", {"points": _ZIG, "closed": True, "smooth": False}),
    ("path", {"points": [[0, 0], [100, 100]], "closed": True}),
    ("path", {"points": [[10, 10]], "closed": False}),
    ("path", {"points": [[0, 0], ["x", 5], [50, 50], [100, None], [100, 0]], "smooth": "false"}),
]

PATHS = [
    (_TRI, False, True), (_TRI, True, True), (_ZIG, False, True), (_ZIG, True, False),
    ([[0, 0], [100, 100]], False, True), ([[5, 5]], False, True), ([], True, True),
]

_L = [[0, 0], [30, 40], [30, 100], [130, 100]]
TRIMS = [(_L, f) for f in (0, -0.5, 0.25, 0.5, 0.999, 1, 2)] + [([[1, 1]], 0.5), ([[0, 0], [0, 0], [10, 0]], 0.5)]

DASHES = [(_L, [10, 10]), (_L, [0, 8]), (_L, [7, 3]), (_L, [300, 1]), (_L, [0, 0]), ([[0, 0]], [4, 4]),
          ([[0, 0], [0, 0], [20, 0]], [5, 5])]

PATTERNS = [("solid", 4), ("dash", 4), ("dot", 4), ("dash", 1.5), ("nada", 2), ("", 2)]

FLIPS = [("arrow", [True, False]), ("arrow", [False, True]), ("triangle", [True, True]), ("check", [True, False]),
         ("heart", [False, False])]

# (campos del clip, instantes locales).
DRAWS = [
    ({"shape": {"type": "check", "draw": 0.4}}, [0.0, 1.0]),
    ({"shape": {"type": "check"}}, [0.0]),
    ({"shape": {"type": "path", "draw": 1.7}}, [0.0]),
    ({"shape": {"type": "check", "draw": 0.0},
      "keyframes": {"enabled": True, "items": [{"t": 0, "props": {"draw": 0}},
                                               {"t": 1.5, "interpolation": "ease-in-out", "props": {"draw": 1}}]}},
     [0.0, 0.25, 0.75, 1.5, 1.9]),
    ({"shape": {"type": "line", "draw": 0.5},
      "keyframes": {"enabled": False, "items": [{"t": 0, "props": {"draw": 0}}, {"t": 1, "props": {"draw": 1}}]}},
     [0.0, 0.5]),
]
