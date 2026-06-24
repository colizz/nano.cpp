#!/usr/bin/env python3
"""Derive narrow correctionlib JME payloads from legacy JetMET txt tarballs.

This script exists for Run-2 AK4PFPuppi subjet payloads that are not available
in the official NanoAODv9 correctionlib JSONs. It also derives AK4PFchs as a
cross-check, because AK4PFchs has an official JSON on CVMFS.

The derived payload is intentionally smaller than the official CVMFS
``jet_jerc.json.gz``. For an AK4PFchs 2018 MC validation payload, it contains:

- JEC levels:
  ``Summer19UL18_V5_MC_L1FastJet_AK4PFchs``
  ``Summer19UL18_V5_MC_L2Relative_AK4PFchs``
  ``Summer19UL18_V5_MC_L3Absolute_AK4PFchs``
  ``Summer19UL18_V5_MC_L2L3Residual_AK4PFchs``
- JES total uncertainty:
  ``Summer19UL18_V5_MC_Total_AK4PFchs``
- JER:
  ``Summer19UL18_JRV3_MC_PtResolution_AK4PFchs``
  ``Summer19UL18_JRV3_MC_ScaleFactor_AK4PFchs``
  ``Summer19UL18_JRV3_MC_SFUncertainty_AK4PFchs``
- One compound correction:
  ``Summer19UL18_V5_MC_L1L2L3Res_AK4PFchs``

It does not derive the full set of official JES uncertainty sources such as
``AbsoluteStat``, ``FlavorQCD``, ``PileUpPtBB``, or ``Regrouped_*``. Only
``Total`` is included.

Usage examples
--------------

Source the LCG runtime before running:

  source /cvmfs/sft.cern.ch/lcg/views/LCG_108/x86_64-el9-gcc13-opt/setup.sh

Validate 2018 AK4PFchs against the official CVMFS latest JSON, including JEC
compound, JRV3 PtResolution, JRV3 ScaleFactor, and JRV3 SFUncertainty:

  python tools/derive_jec_from_txt.py validate-ak4chs \
    --jec-tar /tmp/Summer19UL18_V5_MC.tar.gz \
    --jec-tag Summer19UL18_V5_MC \
    --jer-tar /tmp/Summer19UL18_JRV3_MC.tar.gz \
    --jer-tag Summer19UL18_JRV3_MC \
    --official-json /cvmfs/cms-griddata.cern.ch/cat/metadata/JME/Run2-2018-UL-NanoAODv9/latest/jet_jerc.json.gz \
    --n-points 1000 \
    --tolerance 1e-5

Derive the 2018 AK4PFPuppi payload used for local subjet corrections:

  python tools/derive_jec_from_txt.py derive \
    --jec-tar /tmp/Summer19UL18_V5_MC.tar.gz \
    --jec-tag Summer19UL18_V5_MC \
    --algo AK4PFPuppi \
    --jer-tar /tmp/Summer19UL18_JRV3_MC.tar.gz \
    --jer-tag Summer19UL18_JRV3_MC \
    --output data/jme-derived/Run2-2018-UL-NanoAODv9/2026-06-25/jet_jerc.json.gz
"""

from __future__ import annotations

import argparse
import gzip
import json
import math
import random
import re
import tarfile
import tempfile
from pathlib import Path
from typing import Any

import correctionlib
from correctionlib import schemav2 as cs


VAR_DESCRIPTIONS = {
    "JetA": "area of the jet",
    "JetEta": "pseudorapidity of the jet",
    "JetPhi": "azimuth of the jet",
    "JetPt": "pT of the jet before specific correction (for JER and uncertainties: after all corrections applied)",
    "Rho": "energy density rho (as measure of PU)",
    "systematic": "JER scale factor systematic direction",
}

FORMULA_SYMBOLS = ["x", "y", "z", "t", "u", "v"]
INPUT_ORDER = {
    frozenset(["JetEta", "JetPt"]): ["JetEta", "JetPt"],
    frozenset(["JetA", "JetEta", "JetPt", "Rho"]): ["JetA", "JetEta", "JetPt", "Rho"],
    frozenset(["JetEta", "JetPt", "Rho"]): ["JetEta", "JetPt", "Rho"],
}


def variable(name: str) -> dict[str, str]:
    if name == "systematic":
        return {"name": name, "type": "string", "description": VAR_DESCRIPTIONS[name]}
    return {"name": name, "type": "real", "description": VAR_DESCRIPTIONS.get(name, name)}


def correction_inputs(names: list[str]) -> list[dict[str, str]]:
    return [variable(name) for name in INPUT_ORDER.get(frozenset(names), names)]


def read_member(tar_path: Path, member_name: str) -> str:
    with tarfile.open(tar_path, "r:*") as archive:
        return archive.extractfile(member_name).read().decode()


def has_member(tar_path: Path, member_name: str) -> bool:
    with tarfile.open(tar_path, "r:*") as archive:
        return member_name in archive.getnames()


def split_header(line: str) -> list[str]:
    line = line.strip()
    if not (line.startswith("{") and line.endswith("}")):
        raise ValueError(f"Invalid txt header: {line}")
    return line[1:-1].split()


def parse_header(line: str) -> tuple[list[str], list[str], str, str]:
    tokens = split_header(line)
    pos = 0
    n_binned = int(tokens[pos])
    pos += 1
    binned_vars = tokens[pos : pos + n_binned]
    pos += n_binned
    n_formula = int(tokens[pos])
    pos += 1
    formula_vars = [] if n_formula == 0 else tokens[pos : pos + n_formula]
    pos += n_formula
    formula = tokens[pos]
    pos += 1
    if formula == '""':
        formula = ""
    if tokens[pos] not in {"Correction", "Resolution", "ScaleFactor"}:
        raise ValueError(f"Unsupported header payload kind in {line}")
    level = tokens[pos + 1] if pos + 1 < len(tokens) else tokens[pos]
    return binned_vars, formula_vars, formula, level


def grouped_edges(rows: list[dict[str, Any]], var: str) -> list[float]:
    edges: list[float] = []
    for row in rows:
        lo, hi = row["bins"][var]
        if not edges:
            edges.append(lo)
        if abs(edges[-1] - lo) > 1e-9:
            raise ValueError(f"Non-contiguous edges for {var}: expected {edges[-1]}, got {lo}")
        edges.append(hi)
    return edges


def clamp_formula_expression(formula: str, formula_vars: list[str]) -> str:
    if not formula:
        return formula
    formula = (
        formula.replace("TMath::Log", "log")
        .replace("TMath::Exp", "exp")
        .replace("TMath::Power", "pow")
        .replace("TMath::Sqrt", "sqrt")
    )
    offset = 2 * len(formula_vars)

    def shift_param(match: re.Match[str]) -> str:
        return f"[{int(match.group(1)) + offset}]"

    out = re.sub(r"\[(\d+)\]", shift_param, formula)
    for idx, symbol in enumerate(FORMULA_SYMBOLS[: len(formula_vars)]):
        clamped = f"max(min({symbol},[{2 * idx + 1}]),[{2 * idx}])"
        out = re.sub(rf"(?<![A-Za-z0-9_]){symbol}(?![A-Za-z0-9_])", clamped, out)
    return out


def formula_content(formula: str, formula_vars: list[str], ranges: list[float], params: list[float]) -> dict[str, Any]:
    if formula == "1":
        parameters = ranges if ranges else [1.0]
        return {
            "nodetype": "formula",
            "expression": "1",
            "parser": "TFormula",
            "variables": [formula_vars[0]] if formula_vars else ["JetPt"],
            "parameters": parameters,
        }
    return {
        "nodetype": "formula",
        "expression": clamp_formula_expression(formula, formula_vars),
        "parser": "TFormula",
        "variables": formula_vars,
        "parameters": ranges + params,
    }


def build_binning(rows: list[dict[str, Any]], binned_vars: list[str], leaf_builder) -> dict[str, Any]:
    var = binned_vars[0]
    groups: list[tuple[tuple[float, float], list[dict[str, Any]]]] = []
    for row in rows:
        edge = row["bins"][var]
        if not groups or groups[-1][0] != edge:
            groups.append((edge, []))
        groups[-1][1].append(row)
    edges: list[float] = []
    for edge, _ in groups:
        lo, hi = edge
        if not edges:
            edges.append(lo)
        if abs(edges[-1] - lo) > 1e-9:
            raise ValueError(f"Non-contiguous edges for {var}: expected {edges[-1]}, got {lo}")
        edges.append(hi)
    if len(binned_vars) == 1:
        content = [leaf_builder(group_rows[0]) for _, group_rows in groups]
    else:
        content = [build_binning(group_rows, binned_vars[1:], leaf_builder) for _, group_rows in groups]
    return {"nodetype": "binning", "input": var, "edges": edges, "content": content, "flow": 1.0}


def parse_formula_table(text: str, name: str, description: str) -> dict[str, Any]:
    lines = [line.strip() for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")]
    binned_vars, formula_vars, formula, _ = parse_header(lines[0])
    rows = []
    for line in lines[1:]:
        values = [float(tok) for tok in line.split()]
        pos = 0
        bins = {}
        for var in binned_vars:
            bins[var] = (values[pos], values[pos + 1])
            pos += 2
        n_values = int(values[pos])
        pos += 1
        payload = values[pos : pos + n_values]
        ranges = payload[: 2 * len(formula_vars)]
        params = payload[2 * len(formula_vars) :]
        rows.append({"bins": bins, "ranges": ranges, "params": params})
    all_vars = binned_vars + formula_vars
    data = build_binning(
        rows,
        binned_vars,
        lambda row: formula_content(formula or "1", formula_vars or ["JetPt"], row["ranges"], row["params"]),
    )
    return {
        "name": name,
        "description": description,
        "version": 3,
        "inputs": correction_inputs(all_vars),
        "output": {"name": "correction", "type": "real"},
        "data": data,
    }


def parse_uncertainty_table(text: str, name: str, description: str) -> dict[str, Any]:
    lines = [line.strip() for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")]
    binned_vars, formula_vars, formula, _ = parse_header(lines[0])
    if binned_vars != ["JetEta"] or formula_vars != ["JetPt"] or formula:
        raise ValueError(f"Unsupported uncertainty format for {name}")
    eta_edges = []
    eta_content = []
    for line in lines[1:]:
        values = [float(tok) for tok in line.split()]
        eta_lo, eta_hi = values[0], values[1]
        if not eta_edges:
            eta_edges.append(eta_lo)
        eta_edges.append(eta_hi)
        n_values = int(values[2])
        triples = values[3 : 3 + n_values]
        pts = triples[0::3]
        values_nominal = triples[1::3]
        eta_content.append(
            {
                "nodetype": "binning",
                "input": "JetPt",
                "edges": pts,
                "content": values_nominal[:-1],
                "flow": "clamp",
            }
        )
    return {
        "name": name,
        "description": description,
        "version": 3,
        "inputs": correction_inputs(["JetEta", "JetPt"]),
        "output": {"name": "correction", "type": "real"},
        "data": {"nodetype": "binning", "input": "JetEta", "edges": eta_edges, "content": eta_content, "flow": -999.0},
    }


def parse_scale_factor_table(text: str, name: str, description: str) -> dict[str, Any]:
    lines = [line.strip() for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")]
    binned_vars, formula_vars, formula, _ = parse_header(lines[0])
    if binned_vars == ["JetEta", "JetPt"] and not formula_vars and formula == "[0]":
        rows = []
        for line in lines[1:]:
            values = [float(tok) for tok in line.split()]
            if int(values[4]) != 1:
                raise ValueError(f"Unsupported JER SF payload length for {name}: {int(values[4])}")
            rows.append({"bins": {"JetEta": (values[0], values[1]), "JetPt": (values[2], values[3])}, "value": values[5]})
        return {
            "name": name,
            "description": description,
            "version": 3,
            "inputs": correction_inputs(["JetEta", "JetPt"]),
            "output": {"name": "correction", "type": "real"},
            "data": build_binning(rows, binned_vars, lambda row: row["value"]),
        }
    if binned_vars != ["JetEta"] or formula_vars or formula != "None":
        raise ValueError(f"Unsupported JER SF format for {name}")
    edges = []
    content = []
    for line in lines[1:]:
        values = [float(tok) for tok in line.split()]
        if not edges:
            edges.append(values[0])
        edges.append(values[1])
        n_values = int(values[2])
        payload = values[3 : 3 + n_values]
        if n_values == 1:
            content.append(payload[0])
        elif n_values == 3:
            content.append(
                {
                    "nodetype": "category",
                    "input": "systematic",
                    "content": [
                        {"key": "nom", "value": payload[0]},
                        {"key": "down", "value": payload[1]},
                        {"key": "up", "value": payload[2]},
                    ],
                }
            )
        else:
            raise ValueError(f"Unsupported JER SF payload length for {name}: {n_values}")
    inputs = ["JetEta", "JetPt"] if all(isinstance(item, float) for item in content) else ["JetEta", "JetPt", "systematic"]
    return {
        "name": name,
        "description": description,
        "version": 3,
        "inputs": correction_inputs(inputs),
        "output": {"name": "correction", "type": "real"},
        "data": {"nodetype": "binning", "input": "JetEta", "edges": edges, "content": content, "flow": 1.0},
    }


def parse_sf_uncertainty_table(text: str, name: str, description: str) -> dict[str, Any]:
    lines = [line.strip() for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")]
    binned_vars, formula_vars, formula, _ = parse_header(lines[0])
    if binned_vars != ["JetEta"] or formula_vars != ["JetPt"] or formula:
        raise ValueError(f"Unsupported JER SF uncertainty format for {name}")
    eta_edges = []
    eta_content = []
    for line in lines[1:]:
        values = [float(tok) for tok in line.split()]
        if not eta_edges:
            eta_edges.append(values[0])
        eta_edges.append(values[1])
        n_values = int(values[2])
        payload = values[3 : 3 + n_values]
        if n_values != 3:
            raise ValueError(f"Unsupported JER SF uncertainty payload length for {name}: {n_values}")
        eta_content.append(
            {
                "nodetype": "binning",
                "input": "JetPt",
                "edges": [0.0, payload[0]],
                "content": [payload[-1]],
                "flow": "clamp",
            }
        )
    return {
        "name": name,
        "description": description,
        "version": 3,
        "inputs": correction_inputs(["JetEta", "JetPt"]),
        "output": {"name": "correction", "type": "real"},
        "data": {"nodetype": "binning", "input": "JetEta", "edges": eta_edges, "content": eta_content, "flow": 1.0},
    }


def make_compound(tag: str, algo: str, levels: list[str], inputs: list[str]) -> dict[str, Any]:
    return {
        "name": f"{tag}_L1L2L3Res_{algo}",
        "description": f"compound correction for {algo} derived from legacy txt payloads",
        "inputs": correction_inputs(inputs),
        "output": {"name": "correction", "type": "real"},
        "inputs_update": ["JetPt"],
        "input_op": "*",
        "output_op": "*",
        "stack": [f"{tag}_{level}_{algo}" for level in levels],
    }


def build_payload(jec_tar: Path, tag: str, algo: str, jec_levels: list[str], jer_tar: Path | None, jer_tag: str | None) -> dict[str, Any]:
    corrections = []
    for level in jec_levels:
        member = f"{tag}_{level}_{algo}.txt"
        corrections.append(parse_formula_table(read_member(jec_tar, member), f"{tag}_{level}_{algo}", f"{level} for {algo}"))
    unc_member = f"{tag}_Uncertainty_{algo}.txt"
    corrections.append(parse_uncertainty_table(read_member(jec_tar, unc_member), f"{tag}_Total_{algo}", f"Total JES uncertainty for {algo}"))
    compounds = [make_compound(tag, algo, jec_levels, ["JetA", "JetEta", "JetPt", "Rho"] if jec_levels[0] == "L1FastJet" else ["JetEta", "JetPt"])]
    if jer_tar and jer_tag:
        corrections.append(
            parse_formula_table(
                read_member(jer_tar, f"{jer_tag}_PtResolution_{algo}.txt"),
                f"{jer_tag}_PtResolution_{algo}",
                f"JER pT resolution for {algo}",
            )
        )
        corrections.append(
            parse_scale_factor_table(
                read_member(jer_tar, f"{jer_tag}_SF_{algo}.txt"),
                f"{jer_tag}_ScaleFactor_{algo}",
                f"JER scale factor for {algo}",
            )
        )
        sf_unc_member = f"{jer_tag}_SFUncertainty_{algo}.txt"
        if has_member(jer_tar, sf_unc_member):
            corrections.append(
                parse_sf_uncertainty_table(
                    read_member(jer_tar, sf_unc_member),
                    f"{jer_tag}_SFUncertainty_{algo}",
                    f"JER scale factor uncertainty for {algo}",
                )
            )
    return {"schema_version": 2, "corrections": corrections, "compound_corrections": compounds}


def write_payload(payload: dict[str, Any], output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    cs.CorrectionSet.model_validate(payload)
    with gzip.open(output, "wt") as handle:
        json.dump(payload, handle, separators=(",", ":"))


def validate_ak4chs(args: argparse.Namespace) -> None:
    with tempfile.TemporaryDirectory() as tmpdir:
        generated = Path(tmpdir) / "jet_jerc.json.gz"
        payload = build_payload(
            args.jec_tar,
            args.jec_tag,
            "AK4PFchs",
            ["L1FastJet", "L2Relative", "L3Absolute", "L2L3Residual"],
            args.jer_tar,
            args.jer_tag,
        )
        write_payload(payload, generated)
        derived = correctionlib.CorrectionSet.from_file(str(generated))
        official = correctionlib.CorrectionSet.from_file(str(args.official_json))
        rng = random.Random(12345)
        max_abs = 0.0
        for _ in range(args.n_points):
            area = rng.uniform(0.1, 1.2)
            eta = rng.uniform(-5.0, 5.0)
            pt = 10 ** rng.uniform(math.log10(15.0), math.log10(2000.0))
            rho = rng.uniform(0.0, 60.0)
            got = derived.compound[f"{args.jec_tag}_L1L2L3Res_AK4PFchs"].evaluate(area, eta, pt, rho)
            exp = official.compound[f"{args.jec_tag}_L1L2L3Res_AK4PFchs"].evaluate(area, eta, pt, rho)
            max_abs = max(max_abs, abs(got - exp))
        print(f"AK4PFchs compound max_abs_diff={max_abs:.6g} over {args.n_points} points")
        if max_abs > args.tolerance:
            raise SystemExit(f"validation failed: max_abs_diff={max_abs} > {args.tolerance}")
        if args.jer_tar and args.jer_tag:
            ptres_name = f"{args.jer_tag}_PtResolution_AK4PFchs"
            sf_name = f"{args.jer_tag}_ScaleFactor_AK4PFchs"
            sf_unc_name = f"{args.jer_tag}_SFUncertainty_AK4PFchs"
            max_ptres_abs = 0.0
            max_sf_abs = 0.0
            max_sf_unc_abs = 0.0
            for _ in range(args.n_points):
                eta = rng.uniform(-4.6, 4.6)
                pt = 10 ** rng.uniform(math.log10(15.0), math.log10(2000.0))
                rho = rng.uniform(0.0, 60.0)
                got_ptres = derived[ptres_name].evaluate(eta, pt, rho)
                exp_ptres = official[ptres_name].evaluate(eta, pt, rho)
                got_sf = derived[sf_name].evaluate(eta, pt)
                exp_sf = official[sf_name].evaluate(eta, pt)
                got_sf_unc = derived[sf_unc_name].evaluate(eta, pt)
                exp_sf_unc = official[sf_unc_name].evaluate(eta, pt)
                max_ptres_abs = max(max_ptres_abs, abs(got_ptres - exp_ptres))
                max_sf_abs = max(max_sf_abs, abs(got_sf - exp_sf))
                max_sf_unc_abs = max(max_sf_unc_abs, abs(got_sf_unc - exp_sf_unc))
            print(f"AK4PFchs JER PtResolution max_abs_diff={max_ptres_abs:.6g} over {args.n_points} points")
            print(f"AK4PFchs JER ScaleFactor max_abs_diff={max_sf_abs:.6g} over {args.n_points} points")
            print(f"AK4PFchs JER SFUncertainty max_abs_diff={max_sf_unc_abs:.6g} over {args.n_points} points")
            if max_ptres_abs > args.tolerance:
                raise SystemExit(f"validation failed: JER PtResolution max_abs_diff={max_ptres_abs} > {args.tolerance}")
            if max_sf_abs > args.tolerance:
                raise SystemExit(f"validation failed: JER ScaleFactor max_abs_diff={max_sf_abs} > {args.tolerance}")
            if max_sf_unc_abs > args.tolerance:
                raise SystemExit(f"validation failed: JER SFUncertainty max_abs_diff={max_sf_unc_abs} > {args.tolerance}")


def derive(args: argparse.Namespace) -> None:
    payload = build_payload(args.jec_tar, args.jec_tag, args.algo, args.jec_levels.split(","), args.jer_tar, args.jer_tag)
    write_payload(payload, args.output)
    print(f"wrote {args.output}")


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(required=True)
    p_derive = sub.add_parser("derive")
    p_derive.add_argument("--jec-tar", type=Path, required=True)
    p_derive.add_argument("--jec-tag", required=True)
    p_derive.add_argument("--algo", required=True)
    p_derive.add_argument("--jec-levels", default="L2Relative,L3Absolute")
    p_derive.add_argument("--jer-tar", type=Path)
    p_derive.add_argument("--jer-tag")
    p_derive.add_argument("--output", type=Path, required=True)
    p_derive.set_defaults(func=derive)

    p_validate = sub.add_parser("validate-ak4chs")
    p_validate.add_argument("--jec-tar", type=Path, required=True)
    p_validate.add_argument("--jec-tag", required=True)
    p_validate.add_argument("--jer-tar", type=Path)
    p_validate.add_argument("--jer-tag")
    p_validate.add_argument("--official-json", type=Path, required=True)
    p_validate.add_argument("--n-points", type=int, default=1000)
    p_validate.add_argument("--tolerance", type=float, default=1e-5)
    p_validate.set_defaults(func=validate_ak4chs)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
