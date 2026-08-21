#!/usr/bin/env python3

import argparse
import math
from pathlib import Path

import ROOT
from correctionlib import CorrectionSet


CASES = (
    ("WJetsCorr_collection_ewk", "evj_pTV_EW", "evj"),
    ("ZJetsCorr_collection_ewk", "eej_pTV_EW", "eej"),
)


def close(left, right):
    return math.isclose(left, right, rel_tol=0.0, abs_tol=1e-12)


def check_case(directory, stem, correction_name, prefix):
    root_file = ROOT.TFile.Open(str(directory / f"{stem}.root"), "READ")
    correction = CorrectionSet.from_file(str(directory / f"{stem}.json.gz"))[correction_name]
    names = {
        "nominal": f"{prefix}_pTV_kappa_EW",
        "d1": f"{prefix}_pTV_d1kappa_EW",
        "d2": f"{prefix}_pTV_d2kappa_EW",
        "d3": f"{prefix}_pTV_d3kappa_EW",
    }
    histograms = {variation: root_file.Get(name) for variation, name in names.items()}

    for variation, histogram in histograms.items():
        for index in range(1, histogram.GetNbinsX() + 1):
            pt = histogram.GetXaxis().GetBinCenter(index)
            expected = histogram.GetBinContent(index)
            observed = correction.evaluate(pt, variation)
            if not close(expected, observed):
                raise AssertionError(f"{stem} {variation} bin {index}: {expected} != {observed}")

    def root_value(histogram, pt):
        if pt < 100.0:
            return 0.0
        lookup = math.nextafter(pt, 0.0) if pt == 6500.0 else pt
        return histogram.GetBinContent(histogram.GetXaxis().FindFixBin(lookup))

    def json_value(variation, pt):
        if pt < 100.0:
            return 0.0
        lookup = math.nextafter(pt, 0.0) if pt == 6500.0 else pt
        return correction.evaluate(lookup, variation)

    for pt in (99.0, 100.0, 6500.0, math.nextafter(6500.0, math.inf), 7000.0):
        root_values = [root_value(histograms[variation], pt) for variation in names]
        json_values = [json_value(variation, pt) for variation in names]
        if any(not close(left, right) for left, right in zip(root_values, json_values)):
            raise AssertionError(f"{stem} boundary pt={pt}: {root_values} != {json_values}")
        root_weight = 1.0 + root_values[0]
        root_uncertainty = 0.5 * abs(root_values[0])
        json_weight = 1.0 + json_values[0]
        json_uncertainty = 0.5 * abs(json_values[0])
        for left, right in (
            (root_weight, json_weight),
            (root_weight + root_uncertainty, json_weight + json_uncertainty),
            (root_weight - root_uncertainty, json_weight - json_uncertainty),
        ):
            if not close(left, right):
                raise AssertionError(f"{stem} weight pt={pt}: {left} != {right}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, required=True)
    args = parser.parse_args()
    directory = args.source_dir / "data/vjets-ewk/2023-08-11"
    for case in CASES:
        check_case(directory, *case)


if __name__ == "__main__":
    main()
