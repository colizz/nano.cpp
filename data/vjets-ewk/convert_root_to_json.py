#!/usr/bin/env python3

import gzip
import json
from pathlib import Path

import ROOT


PAYLOADS = (
    ("WJetsCorr_collection_ewk.root", "WJetsCorr_collection_ewk.json.gz", "evj_pTV_EW", "evj"),
    ("ZJetsCorr_collection_ewk.root", "ZJetsCorr_collection_ewk.json.gz", "eej_pTV_EW", "eej"),
)


def binning(histogram):
    axis = histogram.GetXaxis()
    return {
        "nodetype": "binning",
        "input": "pt",
        "edges": [axis.GetBinLowEdge(index) for index in range(1, histogram.GetNbinsX() + 2)],
        "content": [histogram.GetBinContent(index) for index in range(1, histogram.GetNbinsX() + 1)],
        "flow": 0.0,
    }


def convert(directory, root_name, json_name, correction_name, prefix):
    source = ROOT.TFile.Open(str(directory / root_name), "READ")
    if not source or source.IsZombie():
        raise RuntimeError(f"Cannot open {root_name}")

    names = {
        "nominal": f"{prefix}_pTV_kappa_EW",
        "d1": f"{prefix}_pTV_d1kappa_EW",
        "d2": f"{prefix}_pTV_d2kappa_EW",
        "d3": f"{prefix}_pTV_d3kappa_EW",
    }
    histograms = {key: source.Get(name) for key, name in names.items()}
    if any(not histogram for histogram in histograms.values()):
        raise RuntimeError(f"Missing EWK histogram in {root_name}")

    reference_edges = binning(histograms["nominal"])["edges"]
    if any(binning(histogram)["edges"] != reference_edges for histogram in histograms.values()):
        raise RuntimeError(f"Inconsistent binning in {root_name}")

    payload = {
        "schema_version": 2,
        "description": f"NLO electroweak correction converted from {root_name}",
        "corrections": [
            {
                "name": correction_name,
                "description": "Boson pT correction and uncertainty components",
                "version": 1,
                "inputs": [
                    {"name": "pt", "type": "real", "description": "Generator boson transverse momentum"},
                    {"name": "variation", "type": "string", "description": "nominal, d1, d2, or d3"},
                ],
                "output": {"name": "value", "type": "real"},
                "data": {
                    "nodetype": "category",
                    "input": "variation",
                    "content": [
                        {"key": key, "value": binning(histogram)} for key, histogram in histograms.items()
                    ],
                },
            }
        ],
    }
    encoded = json.dumps(payload, separators=(",", ":"), sort_keys=True).encode()
    with gzip.GzipFile(filename=str(directory / json_name), mode="wb", mtime=0) as output:
        output.write(encoded)


def main():
    directory = Path(__file__).resolve().parent / "2023-08-11"
    for arguments in PAYLOADS:
        convert(directory, *arguments)


if __name__ == "__main__":
    main()
