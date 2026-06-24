# Derived JME payloads

This directory stores correctionlib JSON payloads derived from legacy CMS JetMET JEC/JER text tarballs. They are used for Run 2 AK4PFPuppi subjets because the official NanoAODv9 JME correctionlib payloads provide AK4PFchs jets and AK8PFPuppi fatjets, but not the AK4PFPuppi subjet payloads needed here.

Payloads are stored under date-stamped directories. The current version is `2026-06-25`; there is intentionally no `latest` symlink or `latest` subdirectory.

## 2026-06-25

The `2026-06-25` payloads were derived with `tools/derive_jec_from_txt.py` from the official JEC/JER tarballs in `cms-jet/JECDatabase` and `cms-jet/JRDatabase`.

Each `jet_jerc.json.gz` contains only the narrow AK4PFPuppi subset needed by the framework:

- MC JEC: `L2Relative`, `L3Absolute`, `Total`
- MC JER: `PtResolution`, `ScaleFactor`, `SFUncertainty`
- DATA JEC: `L2Relative`, `L3Absolute`, `Total`
- MC compound correction: `*_MC_L1L2L3Res_AK4PFPuppi`
- DATA compound correction: `*_DATA_L1L2L3Res_AK4PFPuppi`

The payloads do not include the full official list of JES uncertainty sources such as `AbsoluteStat`, `FlavorQCD`, `PileUpPtBB`, or `Regrouped_*`; only `Total` is included.

### Per-era tags

| Directory | MC JEC tag | DATA JEC tag | MC JER tag |
| --- | --- | --- | --- |
| `Run2-2016preVFP-UL-NanoAODv9/2026-06-25` | `Summer19UL16APV_V7_MC` | `Summer19UL16APV_RunBCD_V7_DATA` | `Summer20UL16APV_JRV5_MC` |
| `Run2-2016postVFP-UL-NanoAODv9/2026-06-25` | `Summer19UL16_V7_MC` | `Summer19UL16_RunFGH_V7_DATA` | `Summer20UL16_JRV5_MC` |
| `Run2-2017-UL-NanoAODv9/2026-06-25` | `Summer19UL17_V5_MC` | `Summer19UL17_RunB_V5_DATA` | `Summer19UL17_JRV4_MC` |
| `Run2-2018-UL-NanoAODv9/2026-06-25` | `Summer19UL18_V5_MC` | `Summer19UL18_RunA_V5_DATA` | `Summer19UL18_JRV3_MC` |

### Validation

The derivation script validates AK4PFchs against official CVMFS JSONs because AK4PFchs is available in both the legacy text tarballs and official correctionlib payloads. The 2026-06-25 parser changes were checked with `validate-ak4chs` for all four Run 2 NanoAODv9 eras, including JEC compound, JER `PtResolution`, JER `ScaleFactor`, and JER `SFUncertainty`.
