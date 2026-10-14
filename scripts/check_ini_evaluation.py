"""Execute source-defined expressions in the production original-INI runtime.

Independent expected values cover every pinned operator, including signed zero,
precedence and negative rounding. This is CPU language evidence, not GPU output.
"""
from __future__ import annotations
import json
import math
from pathlib import Path
import struct
import subprocess

from ini_language import inventory

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/ini-evaluation"

OPERATORS = {
    "unary_not": ("!$zero", 1), "bitwise_not": ("~$four", -5),
    "unary_plus": ("+$four", 4), "unary_negate": ("-$four", -4),
    "countbits": ("countbits($four)", 1), "sin": ("sin($zero)", 0), "cos": ("cos($zero)", 1),
    "tan": ("tan($zero)", 0), "asin": ("asin($zero)", 0), "acos": ("acos($one)", 0), "atan": ("atan($zero)", 0),
    "abs": ("abs(-$four)", 4), "sign": ("sign(-$four)", -1), "ceil": ("ceil($one + .25)", 2),
    "floor": ("floor(-$one - .25)", -2), "trunc": ("trunc(-$one - .25)", -1),
    "round": ("round($one + .5)", 2), "frac": ("frac(-$one - .25)", .75),
    "sqrt": ("sqrt($four)", 2), "rsqrt": ("rsqrt($four)", .5), "exp": ("exp($zero)", 1),
    "exp2": ("exp2($two)", 4), "log": ("log($one)", 0), "log2": ("log2($four)", 2),
    "saturate": ("saturate($four)", 1), "random": ("random($one)", [0, 1]),
    "exponent": ("$four ** $two", 16), "multiplication": ("$four * $two", 8),
    "division": ("$four / $two", 2), "floor_division": ("(-$four - 1) // $two", -3),
    "modulus": ("(-$four - 1) % $two", -1), "addition": ("$four + $two", 6),
    "subtraction": ("$four - $two", 2), "left_shift": ("$four << $two", 16), "right_shift": ("$four >> $one", 2),
    "less": ("$four < $two", 0), "less_equal": ("$four <= $four", 1), "greater": ("$four > $two", 1),
    "greater_equal": ("$two >= $four", 0), "equality": ("-$zero == $zero", 1),
    "inequality": ("$four != $two", 1), "identical": ("-$zero === $zero", 0), "not_identical": ("-$zero !== $zero", 1),
    "bitwise_and": ("$four & $two", 0), "bitwise_xor": ("$four ^ $two", 6), "bitwise_or": ("$four | $two", 6),
    "and": ("$four && $two", 1), "or": ("$zero || $two", 1),
}


def main():
    source = {x["name"].removesuffix("_operator") for x in inventory()["operators"]}
    if source != set(OPERATORS):
        raise ValueError(f"Source operator coverage changed: {source ^ set(OPERATORS)}")
    cases = dict(OPERATORS)
    cases.update(precedence=("$two + $four * 3", 14), parentheses=("($two + $four) * 3", 18),
                 exponent_associativity=("$two ** 3 ** 2", 512), unary_precedence=("-$two ** 2", 4),
                 bitwise_precedence=("$four | $two & $one", 4), logical_precedence=("$one || $zero && $zero", 1),
                 parameter=("w17 + x", 7), time=("time", 2.5), frame=("frame_number", 24),
                 indexed_count=("index_count", 30), instances=("instance_count", 1), first_index=("first_index", 0),
                 missing_dispatch=("thread_group_count_x + thread_group_count_y + thread_group_count_z", 0),
                 indirect_offset=("indirect_offset", 0), format_literal=("DXGI_FORMAT_R32_FLOAT", 41),
                 binding_literal=("D3D11_BIND_VERTEX_BUFFER", 1), scientific=("1.25e-2", .0125))
    OUT.mkdir(parents=True, exist_ok=True)
    lines = ["namespace = evaluation", "[Constants]", "global $zero = 0", "global $one = 1",
             "global $two = 2", "global $four = 4", "x = 3", "w17 = 4"]
    for i in range(len(cases)):
        lines.append(f"global $case{i} = -999")
    lines += ["[TextureOverrideEvaluate]", "hash = 12345678", "match_index_count = 30"]
    for i, (expression, _) in enumerate(cases.values()):
        lines.append(f"$case{i} = {expression}")
    fixture = OUT / "operators.ini"
    fixture.write_text("\n".join(lines) + "\n", encoding="ascii")
    run = subprocess.run([str(ROOT / "build/xxmi-runtime/runtime_test.exe"), "--evaluate-language", str(fixture), str(len(cases))],
                         cwd=OUT, capture_output=True, text=True, timeout=30)
    if run.returncode:
        raise RuntimeError(run.stderr)
    values = json.loads(run.stdout)["variable_bits"]
    if len(values) != len(cases):
        raise ValueError("Runtime evaluation returned an incomplete variable set")
    results = []
    for (name, (expression, expected)), bits in zip(cases.items(), values):
        value = struct.unpack("<f", struct.pack("<I", bits))[0]
        ok = (expected[0] <= value <= expected[1] if isinstance(expected, list) else
              math.isclose(value, expected, rel_tol=1e-6, abs_tol=1e-7))
        results.append(dict(case=name, expression=expression, expected=expected, actual=value, passed=ok))
    report = dict(scope=__doc__.strip(), passed=all(r["passed"] for r in results), cases=len(results), results=results)
    (OUT / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="ascii")
    for row in results:
        if not row["passed"]:
            print(json.dumps(row))
    print(f"Original INI evaluation: {sum(r['passed'] for r in results)}/{len(results)} checks passed")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
