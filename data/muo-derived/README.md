# Derived MUO payloads

These correctionlib payloads provide auxiliary inputs for muon corrections used by CMSJMECalculators. The muon scale, resolution, and efficiency corrections are loaded separately from the official CMS MUO payloads.

## randomNumbers.json.gz

This file is copied unchanged from [Bamboo's tests/data/randomNumbers.json.gz](https://gitlab.cern.ch/cms-analysis/general/bamboo/-/blob/ce07bd52afa6ddd7fd7d1aa3a36a6ffd90ad36c8/tests/data/randomNumbers.json.gz) at commit `ce07bd`.

It contains the `stdflat` and `stdnormal` corrections, which use correctionlib's `hashprng` node to generate reproducible random numbers from `pt`, `eta`, `phi`, and an integer event seed. These are random-number generators rather than fitted physics corrections.

`configs/run/zmm_2024_v15.yaml` selects this file as `muon_corrections.smearing_file`. The default `smearing_tool` is `stdflat`: CMSJMECalculators transforms its uniform random output through the Crystal Ball inverse cumulative distribution function and applies the resolution parameters from the official muon scale and smearing payload. The generator requires a separate file because the calculator expects four inputs, while the official payload's `RandomSmearing` correction takes three different inputs (`evtNr`, `lumiNr`, and `phi`).