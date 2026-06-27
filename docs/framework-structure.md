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
5. The channel producer runs `analyze(Event&)`.
6. `OutputModel` receives branch values.
7. `RootOutputFile` writes `Events`, plus filtered `Runs` and `LuminosityBlocks`.

## Configuration sources

- `configs/base.yaml`: shared defaults for JEC/JER, PU, b-tag working points, and year/lumi values.
- `configs/common/nano_branches_*.yaml`: NanoAOD branch catalogues and ROOT types by NanoAOD version.
- `configs/common/stored_tagger_names_*.yaml`, `configs/common/read_branches_*.yaml`: shared channel manifests inherited by runtime cards.
- `configs/run/<channel>_<era>_v<version>.yaml`: executable runtime cards, including the ROOT/TTree `preselection`, triggers, channel options, and common manifest inheritance.
- CLI `--set key=value`: final override layer.

## Current channel implementations

- `src/producers/HeavyFlavMuonSampleProducer.cpp`: muon-channel event selection and channel-specific output branches.
- `src/producers/HeavyFlavMinimalProducer.cpp`: minimal-channel leading-cleaned-AK8 selection and fatjet-only output.
- `src/producers/HeavyFlavBaseProducer.cpp`: shared lepton selection, jet/MET correction hookup, output branch filling, and fatjet-level shared content.

## Python reference

- `references/NanoHRTTools/python/producers/HeavyFlavBaseProducer.py`
- `references/NanoHRTTools/python/producers/HeavyFlavMuonSampleProducer.py`
- `references/NanoHRTTools/python/helpers/`

Use the reference only to port or validate behavior. Do not edit it unless the user explicitly asks.

## Validation path

- `tests/muon_validation_test.py`: muon-channel reference validation for nominal MC/data and 2016APV JME variations.
- `ctest --test-dir build --output-on-failure`: default local verification command.
