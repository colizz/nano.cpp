# Staged Changes

Recorded at: `2026-07-24 15:26:52 CEST`

This document records only the 27 files currently staged in Git. Unstaged and untracked workspace files are excluded.

## 1. Build and Runtime Interfaces

- `CMakeLists.txt`
  - Adds the ROOT `Hist` component and `ROOT::Hist` library.
  - Compiles `MuonCorrection.cpp`, `NloEWWeightProducer.cpp`, `HeavyFlavZbbSampleProducer.cpp`, and `HeavyFlavZmmSampleProducer.cpp`.
- `app/nano_run.cpp`
  - Adds the `zbb` and `zmm` channels to `nano_run`.
  - Adds YAML parsing for `muon_corrections` and `nlo_ew`.
  - The default Muon campaign version is `latest`; the default scale/smearing file is `muon_scalesmearing.json.gz` and the default SF file is `muon_Z.json.gz`.
- `app/nano_merge.cpp`
  - Adds `--variations nominal,jes_up,...` to merge only selected variations.
  - If omitted, all variations are merged; nominal files are also subject to the filter.
  - Invalid, repeated, or incomplete variation arguments produce a usage error.
- `README.md`
  - Adds descriptions and 2024 v15 run examples for the `zbb` and `zmm` channels.
  - Documents the Muon payload path, the default `latest` campaign version, and how to pin a version.
  - Adds an end-to-end example using `scripts/run_2024_z_channel.sh` and describes its arguments.
  - Adds a Condor example using `zbb_2024_v15_MC.yaml`.
  - Adds an example for merging with `nano_merge --variations nominal`.

## 2. Common Configuration and Input Branches

- `configs/base.yaml`
  - Updates the AK4 JEC tags from `Summer24Prompt24_V3` to `Summer24Prompt24_V5`.
  - Updates the AK4 JER tag from `Summer24Prompt24_JRV1_MC` to `Summer24Prompt24_JRV2_MC`.
  - Adds Muon correction configuration:
    - Payload root: `/cvmfs/cms-griddata.cern.ch/cat/metadata/MUO`
    - Campaign: `2024_NanoAODv15`
    - Payload subdirectory: `Run3-24CDEReprocessingFGHIPrompt-Summer24-NanoAODv15`
    - Version: `latest`
    - Files: `muon_scalesmearing.json.gz` and `muon_Z.json.gz`
  - Adds NLO EW configuration:
    - Payload directory: `data/vjets-ewk/2023-08-11`
    - W file/main histogram: `WJetsCorr_collection_ewk.root` / `evj_pTV_kappa_EW`
    - Z file/main histogram: `ZJetsCorr_collection_ewk.root` / `eej_pTV_kappa_EW`
    - W uncertainties: `evj_pTV_d1kappa_EW`, `evj_pTV_d2kappa_EW`, `evj_pTV_d3kappa_EW`
    - Z uncertainties: `eej_pTV_d1kappa_EW`, `eej_pTV_d2kappa_EW`, `eej_pTV_d3kappa_EW`
- `configs/common/read_branches_v9.yaml` and `configs/common/read_branches_v12.yaml`
  - Add `LHE_Vpt`, `LHEPart_pdgId`, and `LHEPart_pt`.
- `configs/common/read_branches_v15.yaml`
  - Adds `Muon_charge`, `Muon_highPtId`, `Muon_pfIsoId`, and `Muon_nTrackerLayers`.
  - Adds `SV_pt`, `LHE_Vpt`, `LHEPart_pdgId`, and `LHEPart_pt`.

## 3. New zbb Channel

Related files:

- `include/nano/producers/HeavyFlavZbbSampleProducer.h`
- `src/producers/HeavyFlavZbbSampleProducer.cpp`
- `configs/run/zbb_2024_v15.yaml`
- `configs/samples/zbb_2024_v15_DATA.yaml`
- `configs/samples/zbb_2024_v15_MC.yaml`

The runtime card uses 2024 NanoAOD `v15`, inherits the base configuration, v15 branches, and stored tagger names, and uses the following preselection:

```text
Sum$(abs(FatJet_eta)<2.4)>1
```

Required triggers are `HLT_AK8PFJet380_SoftDropMass30` and `HLT_AK8PFJet500`.

Event selection and processing:

- Requires two JME-corrected AK8 jets.
- The two jets must have transverse momenta above `450` and `200 GeV`, respectively.
- The two leading jets must satisfy `|DeltaPhi| > pi/2`.
- The runtime card defaults to `require_sv_cut: true`; when enabled, at least two secondary vertices are required.
- A jet is marked as qualified only when it has exactly two linked subjets.
- At least one of the two probe jets must be qualified.
- Both leading probe jets are written to the output; `fj_2_*` branches are generated from the corresponding `fj_1_*` branches.
- Adds `passHTTrig`, recording `HLT_AK8PFJet380_SoftDropMass30`.
- Adds `genVpt`, recording the generator transverse momentum of a hard-process W/Z; if no hard-process copy is found, the maximum W/Z `pT` is used.

Sample configuration:

- DATA: `JetMET0` and `JetMET1`, covering Run2024 C–I and the I v2 NanoAOD v15 datasets.
- MC:
  - `qcd-mg`: 8 QCD HT bins, from `200to400` through `2000`.
  - `top`: 1 `TTto4Q` sample.
  - `v-qq`: 3 W→qq and 3 Z→qq `PTQQ` bins.
  - `vv-qq`: `WWto4Q`, `WZto4Q`, and `ZZto4Q`.

## 4. New zmm Channel and Muon Corrections

Related files:

- `include/nano/producers/HeavyFlavZmmSampleProducer.h`
- `src/producers/HeavyFlavZmmSampleProducer.cpp`
- `include/nano/helpers/MuonCorrection.h`
- `src/helpers/MuonCorrection.cpp`
- `configs/run/zmm_2024_v15.yaml`
- `configs/samples/zmm_2024_v15_DATA.yaml`
- `configs/samples/zmm_2024_v15_MC.yaml`

The runtime card uses 2024 NanoAOD `v15` and the following preselection:

```text
Sum$(Muon_pt>50 && abs(Muon_eta)<2.4 && Muon_highPtId)>0
&& Sum$(Muon_pt>25 && abs(Muon_eta)<2.4 && Muon_highPtId)>1
```

Required triggers are `HLT_Mu50` and `HLT_TkMu50`.

Event selection and output:

- Applies the Muon scale correction; MC receives additional resolution smearing.
- Requires exactly two isolated muons passing the ID, `abs(eta)<2.4`, and `pfIsoId>1` requirements.
- The Muon ID is either `pt>15` with `looseId`, or `pt>30` with `highPtId != 0`.
- The two muons must have opposite charge, with leading/subleading `pT` above `60/30 GeV`.
- The dimuon system must have `pT > 450 GeV` and `70 < m < 110 GeV`.
- Selects corrected AK8 recoil jets with `DeltaR > 0.8` from both muons and keeps only the leading jet.
- Adds `passMuTrig`, `leptonicZ_pt`, `leptonicZ_mass`, and `genVpt`.
- Adds `pt`, `eta`, `phi`, `mass`, and `miniIso` branches for `muon0_*` and `muon1_*`.
- MC adds the following SFs and their `stat/syst` up/down variations: `muonHLTSF`, `muonIDSF`, `muonISOSF`, and `muonIDISOSF`.

Muon correction implementation:

- Looks up the campaign using `era + "_NanoAOD" + nano_version`.
- Reads data/MC scale, random smearing, and scale factors from correctionlib payloads.
- MC smearing uses the event number, luminosity block, and phi to generate reproducible random inputs, followed by an inverse Crystal Ball CDF.
- Scale factors are multiplied across the two muons; muons outside the eta or minimum-pT range return a factor of 1.
- Corrected pT is protected against non-finite values, excessive changes, and crossing `200 GeV`; invalid results fall back to the original pT.

Sample configuration:

- DATA: `Muon0` and `Muon1`, covering Run2024 C–I and the I v2 NanoAOD v15 datasets.
- MC:
  - `DY`: 3 `PTLL` bins (200, 400, and 600).
  - `top`: `TTto2L2Nu`, `TWminusto2L2Nu`, and `TbarWplusto2L2Nu`.
  - `v-lep`: `WWto2L2Nu`, `WZto2L2Q`, and `ZZto2L2Q`.

## 5. Common Outputs, NLO EW, and the HeavyFlav Base Class

Related files:

- `include/nano/producers/HeavyFlavBaseProducer.h`
- `src/producers/HeavyFlavBaseProducer.cpp`
- `include/nano/helpers/NloEWWeightProducer.h`
- `src/helpers/NloEWWeightProducer.cpp`

`ProducerConfig` additions:

- `MuonEraConfig` and the Muon campaign map.
- `NloEWConfig`, including W/Z files, the nominal histogram, and three uncertainty histograms for each boson.
- Boolean, numeric, and string maps in `channel_options`.

New common HeavyFlav outputs and logic:

- `LHE_Vpt`: for MC DY, reads the physical branch first; for W/Z, searches `LHEPart` by PDG ID `24/23` and falls back to the physical branch when needed; DATA is set to `-1`.
- `genVpt`: prefers W/Z particles with the hard-process flag in `GenPart`, otherwise uses the maximum W/Z `pT`.
- `nlo_ew_weight`, `nlo_ew_weight_up`, and `nlo_ew_weight_down`, all initialized to `1`.
- For zbb, generates the complete set of `fj_2_*` default branches automatically.
- Adds `fj_1_parTmass = rawMass * globalParT3_massCorrX2p`.
- When filling zbb jets, preserves the leading-jet values and writes the second jet to `fj_2_*`.

NLO EW weight logic:

- Samples starting with `Wto` or `WJetsTo` use the W payload.
- Samples starting with `Zto`, `ZJetsTo`, `DYto`, or `DYJetsTo` use the Z payload.
- Uses the hard-process W/Z `pT`; values below `100 GeV` retain the default weight.
- The nominal weight is `1 + kappa`; the three uncertainty histogram deviations are combined in quadrature for the up/down weights.
- ROOT histograms are cloned and detached from the source file; missing files, histograms, or uncertainty entries raise an error.

## 6. Sample Cross Sections and Correction Data

- `configs/samples/xsec_2024.conf`
  - Adds 2024 zbb/zmm MC cross sections in pb.
  - Covers `TTto4Q`, 8 QCD HT bins, W/Z→qq, four-quark VV, 3 DY `PTLL` bins, dileptonic/single-top samples, and leptonic VV samples.
  - The values are documented as being ported from NanoHRTTools `samples_nanov15/xsec_2024.conf`.
- `data/vjets-ewk/2023-08-11/WJetsCorr_collection_ewk.root`
  - Adds the W+jets NLO EW nominal weight and three uncertainty histogram payloads.
- `data/vjets-ewk/2023-08-11/ZJetsCorr_collection_ewk.root`
  - Adds the Z+jets/DY NLO EW nominal weight and three uncertainty histogram payloads.

## 7. Staged File List

The 27 staged files are:

```text
CMakeLists.txt
README.md
app/nano_merge.cpp
app/nano_run.cpp
configs/base.yaml
configs/common/read_branches_v12.yaml
configs/common/read_branches_v15.yaml
configs/common/read_branches_v9.yaml
configs/run/zbb_2024_v15.yaml
configs/run/zmm_2024_v15.yaml
configs/samples/xsec_2024.conf
configs/samples/zbb_2024_v15_DATA.yaml
configs/samples/zbb_2024_v15_MC.yaml
configs/samples/zmm_2024_v15_DATA.yaml
configs/samples/zmm_2024_v15_MC.yaml
data/vjets-ewk/2023-08-11/WJetsCorr_collection_ewk.root
data/vjets-ewk/2023-08-11/ZJetsCorr_collection_ewk.root
include/nano/helpers/MuonCorrection.h
include/nano/helpers/NloEWWeightProducer.h
include/nano/producers/HeavyFlavBaseProducer.h
include/nano/producers/HeavyFlavZbbSampleProducer.h
include/nano/producers/HeavyFlavZmmSampleProducer.h
src/helpers/MuonCorrection.cpp
src/helpers/NloEWWeightProducer.cpp
src/producers/HeavyFlavBaseProducer.cpp
src/producers/HeavyFlavZbbSampleProducer.cpp
src/producers/HeavyFlavZmmSampleProducer.cpp
```

The following are outside the scope of this document: `CMSJMECalculators`, `templates/condor/process.sh.in`, `scripts/`, and all other unstaged or untracked files.
