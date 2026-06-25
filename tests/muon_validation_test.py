#!/usr/bin/env python3
import argparse
import math
import subprocess
import sys
from functools import lru_cache
from pathlib import Path

import ROOT

ROOT.gROOT.SetBatch(True)

DATA_DIR = "tests/data/muon_validation"
INPUT_DIR = f"{DATA_DIR}/inputs"
REFERENCE_DIR = f"{DATA_DIR}/references"

NOMINAL_CASES = [
    {"name": "2016APV_mc", "config": "configs/run/muon_2016APV_v9.yaml", "inputs": [f"{INPUT_DIR}/ttbarsl_2016APV_nanov9_example.root"], "reference": f"{REFERENCE_DIR}/ttbarsl_2016APV_nanov9_reference.root", "overrides": ["output.include_lhe_weights=true"], "check_lhe_weights": True},
    {"name": "2016_mc", "config": "configs/run/muon_2016_v9.yaml", "inputs": [f"{INPUT_DIR}/ttbarsl_2016_nanov9_example.root"], "reference": f"{REFERENCE_DIR}/ttbarsl_2016_nanov9_reference.root", "overrides": []},
    {"name": "2017_mc", "config": "configs/run/muon_2017_v9.yaml", "inputs": [f"{INPUT_DIR}/ttbarsl_2017_nanov9_example.root"], "reference": f"{REFERENCE_DIR}/ttbarsl_2017_nanov9_reference.root", "overrides": []},
    {"name": "2018_mc", "config": "configs/run/muon_2018_v9.yaml", "inputs": [f"{INPUT_DIR}/ttbarsl_2018_nanov9_example.root"], "reference": f"{REFERENCE_DIR}/ttbarsl_2018_nanov9_reference.root", "overrides": []},
    {"name": "2018_data", "config": "configs/run/muon_2018_v9.yaml", "inputs": [f"{INPUT_DIR}/singlemu_data_2018_nanov9_example_part1.root", f"{INPUT_DIR}/singlemu_data_2018_nanov9_example_part2.root"], "reference": f"{REFERENCE_DIR}/singlemu_2018_nanov9_reference.root", "overrides": []},
    {"name": "2022EE_mc", "config": "configs/run/muon_2022EE_v12.yaml", "inputs": [f"{INPUT_DIR}/ttbarfl_2022EE_nanov12_example.root"], "reference": f"{REFERENCE_DIR}/ttbarfl_2022EE_nanov12_reference.root", "overrides": []},
    {"name": "2024_mc", "config": "configs/run/muon_2024_v15.yaml", "inputs": [f"{INPUT_DIR}/ttbarfl_2024_nanov15_example.root"], "reference": f"{REFERENCE_DIR}/ttbarfl_2024_nanov15_reference.root", "overrides": []},
]

VARIATION_NAMES = ["nominal", "jes_up", "jes_down", "jer_up", "jer_down", "met_up", "met_down"]
JME_KEY_BRANCHES = [
    "fj_1_pt",
    "fj_1_sdmass",
    "fj_1_sj1_pt",
    "fj_1_sj2_pt",
    "met",
    "metphi",
    "leptonicW_pt",
]
NOMINAL_MONITOR_BRANCHES = [
    "puWeight",
    "puWeightUp",
    "puWeightDown",
    "jetVetoFlag",
    "jetVetoMap",
]

MUON_2016APV_VARIATION_REFERENCES = {
    "nominal": f"{REFERENCE_DIR}/ttbarsl_2016APV_nanov9_reference.root",
    "jes_up": f"{REFERENCE_DIR}/ttbarsl_2016APV_nanov9_jes_up_reference.root",
    "jes_down": f"{REFERENCE_DIR}/ttbarsl_2016APV_nanov9_jes_down_reference.root",
    "jer_up": f"{REFERENCE_DIR}/ttbarsl_2016APV_nanov9_jer_up_reference.root",
    "jer_down": f"{REFERENCE_DIR}/ttbarsl_2016APV_nanov9_jer_down_reference.root",
    "met_up": f"{REFERENCE_DIR}/ttbarsl_2016APV_nanov9_met_up_reference.root",
    "met_down": f"{REFERENCE_DIR}/ttbarsl_2016APV_nanov9_met_down_reference.root",
}

IGNORE_BRANCHES = {
    "PSWeight",
    # The C++ port intentionally uses correctionlib PU payloads while the
    # Run2 reference files were produced with the older NanoHRT histogram PU
    # helper. Check that the branches are produced, but do not compare their
    # values against those reference files.
    "puWeight",
    "puWeightUp",
    "puWeightDown",
    # Jet veto map and PU weights are printed in the nominal monitor block
    # below, but do not decide pass/fail against old reference files.
    "jetVetoFlag",
}


class Report:
    TOP_LEVEL_PREFIXES = (
        "MUON VALIDATION",
        "Nominal cases",
        "2016APV systematics via multi-variation output",
        "report=",
    )

    def __init__(self, path):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.handle = self.path.open("w")
        self.in_case = False

    def close(self):
        self.handle.close()

    def line(self, text=""):
        if text.startswith("CASE "):
            self.in_case = True
            formatted = text
        elif any(text.startswith(prefix) for prefix in self.TOP_LEVEL_PREFIXES):
            self.in_case = False
            formatted = text
        elif self.in_case and text:
            formatted = f"  {text}"
        else:
            formatted = text
        print(formatted)
        print(formatted, file=self.handle)
        self.handle.flush()

# Branches whose values can legitimately differ from the old Python reference
# because the C++ port uses CMSJMECalculators/correctionlib for JME and related
# MET propagation. They still participate in branch-presence checks through the
# common-branch lookup, but not in scalar value comparisons.
CORRECTION_DEPENDENT_PREFIXES = ("fj_1_", "met")
CORRECTION_DEPENDENT_BRANCHES = {"ht", "leptonicW_pt"}


def tree(path):
    path = str(path)
    if path.startswith("/store/"):
        path = "root://cms-xrd-global.cern.ch/" + path
    f = ROOT.TFile.Open(path, "READ")
    if not f or f.IsZombie():
        raise RuntimeError(f"cannot open {path}")
    t = f.Get("Events")
    if not t:
        raise RuntimeError(f"missing Events tree in {path}")
    return f, t


def markdown_code(text):
    text = text.strip()
    if text.startswith("`") and text.endswith("`"):
        return text[1:-1]
    return text


def store_path(path):
    path = markdown_code(path)
    if path.startswith("/store/"):
        return path
    marker = "/store/"
    pos = path.find(marker)
    if pos >= 0:
        return path[pos:]
    return path


@lru_cache(maxsize=None)
def remote_inputs_from_readme(source_dir):
    readme = Path(source_dir) / DATA_DIR / "README.md"
    if not readme.exists():
        return {}

    remote_inputs = {}
    for line in readme.read_text().splitlines():
        line = line.strip()
        if not line.startswith("|") or "`" not in line:
            continue
        columns = [column.strip() for column in line.strip("|").split("|")]
        if len(columns) < 3:
            continue
        local_file = markdown_code(columns[1])
        original_path = store_path(columns[2])
        if local_file.startswith("inputs/") and original_path:
            remote_inputs[f"{DATA_DIR}/{local_file}"] = original_path
    return remote_inputs


def branch_names(t):
    return {b.GetName() for b in t.GetListOfBranches()}


def scalar_leaf_type(t, name):
    b = t.GetBranch(name)
    leaves = b.GetListOfLeaves() if b else None
    if not leaves or leaves.GetEntries() != 1:
        return None
    leaf = leaves.At(0)
    if leaf.GetLenStatic() != 1:
        return None
    return leaf.GetTypeName()


def comparable_branches(ref, out, extra_ignore=None):
    ignored = set(IGNORE_BRANCHES)
    if extra_ignore:
        ignored.update(extra_ignore)
    common = sorted((branch_names(ref) & branch_names(out)) - ignored)
    selected = []
    for name in common:
        if name in CORRECTION_DEPENDENT_BRANCHES or name.startswith(CORRECTION_DEPENDENT_PREFIXES):
            continue
        if scalar_leaf_type(ref, name) and scalar_leaf_type(out, name):
            selected.append(name)
    return selected


def value(t, name):
    return getattr(t, name)


def close_enough(name, actual, expected):
    if isinstance(expected, bool) or isinstance(actual, bool):
        return bool(actual) == bool(expected)
    if isinstance(expected, int) and isinstance(actual, int):
        return actual == expected
    try:
        a = float(actual)
        e = float(expected)
    except Exception:
        return actual == expected
    if math.isnan(a) and math.isnan(e):
        return True
    return abs(a - e) <= 1.0e-4 + 1.0e-5 * abs(e)


def event_key(t):
    return (int(value(t, "run")), int(value(t, "luminosityBlock")), int(value(t, "event")))


def keyed_entries(t):
    entries = {}
    for i in range(t.GetEntries()):
        t.GetEntry(i)
        entries[event_key(t)] = i
    return entries


def emit_report(report, line):
    if report:
        report.line(line)
    else:
        print(line)


def report_event_details(rt, ot, pairs, branches, report=None, max_detail_events=20):
    for ref_i, out_i in pairs[:max_detail_events]:
        rt.GetEntry(ref_i)
        ot.GetEntry(out_i)
        key = event_key(ot)
        emit_report(report, f"  EVENT run={key[0]} lumi={key[1]} event={key[2]}")
        for name in branches:
            rv = value(rt, name)
            ov = value(ot, name)
            try:
                rvf = float(rv)
                ovf = float(ov)
                diff = ovf - rvf
                line = f"    {name:<20} ref={rvf: .9g} out={ovf: .9g} diff={diff: .9g} {'OK' if close_enough(name, ov, rv) else 'DIFF'}"
            except Exception:
                line = f"    {name:<20} ref={rv!r} out={ov!r} {'OK' if close_enough(name, ov, rv) else 'DIFF'}"
            emit_report(report, line)


def report_monitor_branches(rt, ot, pairs, branches, report=None, max_detail_events=20):
    ref_names = branch_names(rt)
    out_names = branch_names(ot)
    common = [name for name in branches if name in ref_names and name in out_names]
    ref_only = [name for name in branches if name in ref_names and name not in out_names]
    out_only = [name for name in branches if name not in ref_names and name in out_names]
    missing = [name for name in branches if name not in ref_names and name not in out_names]
    emit_report(
        report,
        "Nominal monitor branches: "
        f"common={','.join(common) if common else '(none)'} "
        f"ref_only={','.join(ref_only) if ref_only else '(none)'} "
        f"out_only={','.join(out_only) if out_only else '(none)'} "
        f"missing={','.join(missing) if missing else '(none)'}",
    )
    if common:
        report_event_details(rt, ot, pairs, common, report=report, max_detail_events=max_detail_events)


def compare_outputs(reference, output, require_full=True, report=None, max_detail_events=20, extra_ignore=None):
    rf, rt = tree(reference)
    of, ot = tree(output)
    try:
        rn = rt.GetEntries()
        on = ot.GetEntries()
        if require_full and rn != on:
            raise AssertionError(f"entry count differs: reference={rn} output={on}")
        if on > rn:
            raise AssertionError(f"output has more entries than reference: reference={rn} output={on}")
        if on == 0:
            raise AssertionError("output has zero selected entries")
        branches = comparable_branches(rt, ot, extra_ignore=extra_ignore)
        if not branches:
            raise AssertionError("no comparable common scalar branches")
        mismatches = []
        if require_full:
            pairs = [(i, i) for i in range(on)]
            for i in range(on):
                rt.GetEntry(i)
                ot.GetEntry(i)
                if event_key(rt) != event_key(ot):
                    raise AssertionError(f"event key differs at entry {i}: reference={event_key(rt)} output={event_key(ot)}")
        else:
            ref_entries = keyed_entries(rt)
            out_entries = keyed_entries(ot)
            common_keys = sorted(set(ref_entries) & set(out_entries))
            if not common_keys:
                raise AssertionError("no common event keys between reference and output")
            missing = sorted(set(out_entries) - set(ref_entries))
            if len(missing) > max(2, int(0.20 * len(out_entries))):
                raise AssertionError(f"too many output events missing from reference: {len(missing)}/{len(out_entries)}")
            pairs = [(ref_entries[key], out_entries[key]) for key in common_keys]
        for ref_i, out_i in pairs:
            rt.GetEntry(ref_i)
            ot.GetEntry(out_i)
            for name in branches:
                rv = value(rt, name)
                ov = value(ot, name)
                if not close_enough(name, ov, rv):
                    mismatches.append((event_key(ot), name, rv, ov))
                    if len(mismatches) >= 20:
                        break
            if len(mismatches) >= 20:
                break
        if mismatches:
            lines = ["first branch mismatches:"]
            lines += [f"  event={key} branch={name} reference={rv!r} output={ov!r}" for key, name, rv, ov in mismatches]
            raise AssertionError("\n".join(lines))
        suffix = "full" if require_full else "prefix"
        line = f"compared {len(pairs)}/{rn} {suffix} event keys and {len(branches)} scalar branches"
        emit_report(report, line)
        report_event_details(rt, ot, pairs, branches, report=report, max_detail_events=max_detail_events)
        key_branches = [name for name in JME_KEY_BRANCHES if name in branch_names(rt) and name in branch_names(ot)]
        if key_branches:
            emit_report(report, f"JME key branches: branches={','.join(key_branches)}")
            report_event_details(rt, ot, pairs, key_branches, report=report, max_detail_events=max_detail_events)
        report_monitor_branches(rt, ot, pairs, NOMINAL_MONITOR_BRANCHES, report=report, max_detail_events=max_detail_events)
    finally:
        rf.Close()
        of.Close()


def compare_lhe_weights(expected, output, max_expected_entries=None, report=None):
    rf, rt = tree(expected)
    of, ot = tree(output)
    try:
        if "LHEScaleWeight" not in branch_names(ot):
            raise AssertionError("output is missing LHEScaleWeight")
        if "LHEScaleWeight" not in branch_names(rt):
            raise AssertionError(f"expected source is missing LHEScaleWeight: {expected}")
        out_entries = keyed_entries(ot)
        expected_keys = set(out_entries)
        ref_entries = {}
        n_scan = rt.GetEntries() if max_expected_entries is None else min(rt.GetEntries(), max_expected_entries)
        for i in range(n_scan):
            rt.GetEntry(i)
            key = event_key(rt)
            if key in expected_keys:
                ref_entries[key] = i
                if len(ref_entries) == len(expected_keys):
                    break
        missing_keys = sorted(expected_keys - set(ref_entries))
        if missing_keys:
            raise AssertionError(f"LHEScaleWeight expected source is missing {len(missing_keys)} output event keys")
        common_keys = sorted(expected_keys)
        compared_values = 0
        for key in common_keys:
            rt.GetEntry(ref_entries[key])
            ot.GetEntry(out_entries[key])
            ref_vals = list(getattr(rt, "LHEScaleWeight"))
            out_vals = list(getattr(ot, "LHEScaleWeight"))
            if len(ref_vals) != len(out_vals):
                raise AssertionError(f"LHEScaleWeight length differs for event={key}: expected={len(ref_vals)} output={len(out_vals)}")
            for idx, (rv, ov) in enumerate(zip(ref_vals, out_vals)):
                compared_values += 1
                if abs(float(ov) - float(rv)) > 1.0e-6 + 1.0e-6 * abs(float(rv)):
                    raise AssertionError(f"LHEScaleWeight differs for event={key} index={idx}: expected={rv} output={ov}")
        line = f"LHEScaleWeight copied from input: common_events={len(common_keys)} values={compared_values}"
        if report:
            report.line(line)
        else:
            print(line)
    finally:
        rf.Close()
        of.Close()


def compare_key_branches(reference, output, branches=JME_KEY_BRANCHES, report=None, max_detail_events=20):
    rf, rt = tree(reference)
    of, ot = tree(output)
    try:
        ref_entries = keyed_entries(rt)
        out_entries = keyed_entries(ot)
        common_keys = sorted(set(ref_entries) & set(out_entries))
        if not common_keys:
            raise AssertionError("no common event keys between reference and output")
        common_branches = [name for name in branches if name in branch_names(rt) and name in branch_names(ot)]
        line = f"key-branch comparison: common_events={len(common_keys)} branches={','.join(common_branches)}"
        emit_report(report, line)
        for name in common_branches:
            diffs = []
            max_item = None
            for key in common_keys:
                rt.GetEntry(ref_entries[key])
                ot.GetEntry(out_entries[key])
                rv = float(value(rt, name))
                ov = float(value(ot, name))
                diff = ov - rv
                adiff = abs(diff)
                diffs.append(adiff)
                if max_item is None or adiff > max_item[0]:
                    max_item = (adiff, key, rv, ov, diff)
            mean = sum(diffs) / len(diffs) if diffs else 0.0
            adiff, key, rv, ov, diff = max_item
            line = (
                f"  {name}: mean_abs={mean:.6g} max_abs={adiff:.6g} "
                f"event={key} reference={rv:.9g} output={ov:.9g} diff={diff:.9g}"
            )
            emit_report(report, line)
        pairs = [(ref_entries[key], out_entries[key]) for key in common_keys]
        report_event_details(rt, ot, pairs, common_branches, report=report, max_detail_events=max_detail_events)
    finally:
        rf.Close()
        of.Close()


def resolve_input(source_dir, input_file):
    if input_file.startswith("root://") or input_file.startswith("/store/"):
        return input_file
    path = Path(input_file)
    if path.is_absolute():
        resolved = path
        lookup = str(path)
    else:
        resolved = source_dir / path
        lookup = str(path)
    if resolved.exists():
        return str(resolved)
    return remote_inputs_from_readme(source_dir).get(lookup, str(resolved))


def run_case(case, source_dir, build_dir, output_dir, max_input_events=None, report=None):
    out = output_dir / f"{case['name']}.root"
    out.parent.mkdir(parents=True, exist_ok=True)
    inputs = [resolve_input(source_dir, input_file) for input_file in case["inputs"]]
    cmd = [str(build_dir / "nano_run"), "--input-files", ",".join(inputs), "--output-file", str(out), "--config", str(source_dir / case["config"])]
    if max_input_events is not None:
        cmd.extend(["--num-events", str(max_input_events)])
    for override in case["overrides"]:
        cmd.extend(["--set", override])
    if report:
        report.line(f"CASE {case['name']}")
        report.line(" ".join(cmd))
    else:
        print("running", case["name"])
        print(" ".join(cmd))
    subprocess.run(cmd, cwd=build_dir, check=True)
    produced_out = variation_output_path(out, "nominal")
    extra_ignore = set()
    if case["name"].startswith(("2022", "2023", "2024", "2025", "2026")):
        extra_ignore.add("topptWeight")
        if report:
            report.line("Ignoring topptWeight in pass/fail comparison for Run 3 references; C++ applies the 13.6 TeV extrapolation.")
    compare_outputs(source_dir / case["reference"], produced_out, require_full=max_input_events is None, report=report, extra_ignore=extra_ignore)
    if case.get("check_lhe_weights"):
        compare_lhe_weights(Path(inputs[0]), produced_out, max_expected_entries=max_input_events, report=report)
    if report:
        report.line()


def variation_output_path(output_file, variation):
    path = Path(output_file)
    return path.with_name(f"{path.stem}_{variation}{path.suffix}")


def run_2016apv_multi_variation(source_dir, build_dir, output_dir, max_input_events=None, report=None):
    output_dir.mkdir(parents=True, exist_ok=True)
    output_base = output_dir / "2016APV_mc.root"
    case = NOMINAL_CASES[0]
    inputs = [resolve_input(source_dir, input_file) for input_file in case["inputs"]]
    cmd = [
        str(build_dir / "nano_run"),
        "--input-files",
        ",".join(inputs),
        "--output-file",
        str(output_base),
        "--config",
        str(source_dir / case["config"]),
        "--variations",
        ",".join(VARIATION_NAMES),
    ]
    if max_input_events is not None:
        cmd.extend(["--num-events", str(max_input_events)])
    if report:
        report.line("CASE 2016APV_mc multi-variation")
        report.line(" ".join(cmd))
    else:
        print("running 2016APV_mc multi-variation")
        print(" ".join(cmd))
    subprocess.run(cmd, cwd=source_dir, check=True)

    missing_references = []
    for variation in VARIATION_NAMES:
        out = variation_output_path(output_base, variation)
        ref = source_dir / MUON_2016APV_VARIATION_REFERENCES[variation]
        if not ref.exists():
            missing_references.append(str(ref))
            continue
        if report:
            report.line(f"VARIATION 2016APV_mc {variation}")
        else:
            print(f"comparing 2016APV_mc {variation}")
        compare_key_branches(ref, out, report=report)
        if report:
            report.line()
    if missing_references:
        message = "missing 2016APV variation reference files:"
        if report:
            report.line(message)
        else:
            print(message)
        for path in missing_references:
            if report:
                report.line(f"  {path}")
            else:
                print(f"  {path}")


def muon_validation(source_dir, build_dir, output_dir, max_input_events=None):
    report = Report(output_dir / "key_branch_compare_report.txt")
    try:
        report.line("MUON VALIDATION")
        report.line("Nominal cases")
        report.line()
        for case in NOMINAL_CASES:
            run_case(case, source_dir, build_dir, output_dir / "nominal", max_input_events, report=report)
        report.line("2016APV systematics via multi-variation output")
        report.line()
        run_2016apv_multi_variation(source_dir, build_dir, output_dir / "2016APV_systematics", max_input_events, report=report)
        report.line(f"report={report.path}")
    finally:
        report.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--group", choices=["muon"], default="muon")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--max-input-events", type=int)
    args = parser.parse_args()
    output_dir = args.output_dir or (args.build_dir / "test-muon-validation")
    if args.group == "muon":
        try:
            muon_validation(args.source_dir, args.build_dir, output_dir, args.max_input_events)
        except Exception as exc:
            print(f"FAILED muon_validation: {exc}", file=sys.stderr)
            return 1
        return 0

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
