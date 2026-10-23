# Framework Structure

## Entry points

- `app/nano_run.cpp`: run one channel locally on one or more input ROOT files.
- `app/nano_make_condor.cpp`: build a Condor submission directory around `nano_run`.
- `app/runtime_common.h`: shared CLI, YAML, file-list, and ROOT merge utilities used by both executables.

## Where to read framework logic

- `include/nano/core/`, `src/core/`: event model, object access, geometry helpers, output cache.
- `include/nano/io/`, `src/io/`: ROOT input reader and ROOT output writer.
- `include/nano/helpers/`, `src/helpers/`: reusable physics services such as JME, PU weights, top-pt weights, and gen matching.
- `include/nano/producers/`, `src/producers/`: channel logic and shared producer logic.

## Main execution flow

1. `nano_run` loads YAML config, applies `extends`, then applies CLI `--set` overrides.
2. The merged runtime config's `read_branches` list declares the exact input branches to bind.
3. `nano_run` resolves their types from `configs/common/nano_branches_*.yaml`, and `NanoReader` binds those branches.
4. Each selected entry is wrapped as `Event`.
5. For each event, the runner calls `analyze_common(Event&)` once, using the requested variations stored in `ProducerConfig`. Events that fail this step skip all subsequent processing.
6. For each surviving event, the runner calls `compute_jme_result()` once, then `analyze_variation()` sequentially for each requested variation. Each accepted result is immediately copied from `OutputModel` to the corresponding output file.
7. `RootOutputFile` writes `Events`, plus filtered `Runs` and `LuminosityBlocks`.

## Configuration sources

- `configs/base.yaml`: shared defaults for JEC/JER, PU, b-tag working points, and year/lumi values.
- `configs/common/nano_branches_*.yaml`: NanoAOD branch catalogues and ROOT types by NanoAOD version.
- `configs/common/stored_tagger_names_*.yaml`, `configs/common/read_branches_*.yaml`: shared channel manifests inherited by runtime cards.
- `configs/run/<channel>_<era>_v<version>.yaml`: executable runtime cards, including the ROOT/TTree `preselection`, triggers, channel options, and common manifest inheritance.
- CLI `--set key=value`: final override layer.

`output.fill_subjet_info` defaults to `true` for all channels. Setting it to `false` skips registration and filling of `sj1_*`, `sj2_*`, and `deltaR_sj12` output branches, including QCD subjet SV details and `sj12_masscor_dxysig`. QCD fatjet-level SV counts and track sums remain controlled by `channels.qcd.fill_sv`. Subjet JME and soft-drop mass reconstruction remain active, as does subjet/SV matching required by `channels.qcd.apply_sv_criteria`. For example, use `--set output.fill_subjet_info=false` to disable these details.

## Current channel implementations

- `src/producers/HeavyFlavMuonSampleProducer.cpp`: muon-channel event selection and channel-specific output branches.
- `src/producers/HeavyFlavMinimalProducer.cpp`: minimal-channel leading-cleaned-AK8 selection and fatjet-only output.
- `src/producers/HeavyFlavQCDSampleProducer.cpp`: QCD dijet selection, secondary-vertex matching, two-fatjet output, and optional channel-owned SV detail filling through `channels.qcd.fill_sv`.
- `src/producers/HeavyFlavBaseProducer.cpp`: shared lepton selection, jet/MET correction hookup, output branch filling, and fatjet-level shared content.

## Python reference

- `references/NanoHRTTools/python/producers/HeavyFlavBaseProducer.py`
- `references/NanoHRTTools/python/producers/HeavyFlavMuonSampleProducer.py`
- `references/NanoHRTTools/python/helpers/`

Use the reference only to port or validate behavior. Do not edit it unless the user explicitly asks.

## Validation path

- `tests/muon_validation_test.py`: muon-channel reference validation for nominal MC/data and 2016APV JME variations.
- `ctest --test-dir build --output-on-failure`: default local verification command.
