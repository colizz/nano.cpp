# Derived JME payloads

These correctionlib payloads provide AK4PFPuppi subjet JEC/JER for Run 2 NanoAODv9. The official NanoAODv9 JSONs do not include this jet type.

## Current version: 2026-10-17

The four current payloads are based on the previous `2026-06-25` version. They retain the same era-specific CMS JEC and JER tags listed below.

Changes from `2026-06-25`:

- Rebuilt the JEC entries from the corresponding CMS `JECDatabase` text tarballs. DATA now applies `L2L3Residual` after `L2Relative` and `L3Absolute`.
- Added unity `L1FastJet` entries to MC and DATA, plus a unity MC `L2L3Residual` entry. These give AK4PFPuppi the interface expected by CMSJMECalculators without changing the MC JEC value.
- Set compound inputs to `JetA`, `JetEta`, `JetPt`, `Rho`; DATA compounds with run-dependent corrections take `run` last. Such corrections reject runs outside their configured range.
- Copied the `PtResolution`, `ScaleFactor`, and `SFUncertainty` JER entries unchanged from `2026-06-25`.

Validation: all four JSONs load in correctionlib; sampled MC compound values and JER entries match the previous version. The 2018 corrections match the audited payload in `boohft-calib/study/261016_nanocpp_nanohrt_jer_audit/nano.cpp`. Muon validation passes.

## Previous version: 2026-06-25

This version was derived from CMS `JECDatabase` and `JRDatabase` text tarballs with `tools/derive_jec_from_txt.py`. It remains available under each era's `2026-06-25` directory. Only the `Total` JES uncertainty source is included.

| Era directory | MC JEC tag | DATA JEC tag | MC JER tag |
| --- | --- | --- | --- |
| `Run2-2016preVFP-UL-NanoAODv9` | `Summer19UL16APV_V7_MC` | `Summer19UL16APV_V7_DATA` | `Summer20UL16APV_JRV5_MC` |
| `Run2-2016postVFP-UL-NanoAODv9` | `Summer19UL16_V7_MC` | `Summer19UL16_V7_DATA` | `Summer20UL16_JRV5_MC` |
| `Run2-2017-UL-NanoAODv9` | `Summer19UL17_V5_MC` | `Summer19UL17_V5_DATA` | `Summer19UL17_JRV4_MC` |
| `Run2-2018-UL-NanoAODv9` | `Summer19UL18_V5_MC` | `Summer19UL18_V5_DATA` | `Summer19UL18_JRV3_MC` |

Each version lives at `<era directory>/<version>/jet_jerc.json.gz`. `configs/base.yaml` selects `2026-10-17` for all four eras.
