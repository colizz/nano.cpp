# nano.cpp

`nano.cpp` (also named as `NanoAODTools.Cpp`) is a C++ rewrite of selected [NanoAOD-tools](https://github.com/cms-nanoAOD/nanoAOD-tools)/[NanoHRTTools](https://github.com/hqucms/NanoHRT-tools) workflows.

The goal is to keep the analysis logic human-readable while making the event loop faster and easier to validate. The style is intentionally close to the traditional ROOT event loop:

- read one event
- build objects
- apply selections
- compute new features
- write a skim tree

The guiding idea is:

> Agents write, you review.

The framework is designed so AI agents can write straightforward C++ while humans can still review the physics logic in a direct, readable way.

## Why This Exists

The original NanoAOD-tools code is Python-based and flexible, but it is not as fast as columnar analysis frameworks such as RDataFrame or awkward-array/coffea. This repository keeps the useful NanoAOD-tools programming model and moves the event processing to C++ for faster ntuplization.

The intended style is explicit event-level code:

```cpp
auto fatjets = event.collection("FatJet").objects();

for (auto &jet : event.collection("Jet").objects()) {
  const auto btag = jet.get<float>("btagUParTAK4B");
  if (jet.pt() > 30.0f && std::abs(jet.eta()) < 2.4f && btag > btag_wp) {
    bjets.push_back(jet);
  }
}

event.set("bjets", bjets);
event.set("leptonicW", leptonic_w);

for (auto &fj : fatjets) {
  fj.set("subjets", linked_subjets);
  fj.set("dr_T", delta_r_to_top);
  fj.set("is_qualified", true);
}
```

In practice this means:

- Object collections are accessed from the event, for example `event.collection("FatJet")`.
- NanoAOD branches are accessed as typed object attributes, for example `fj.get<float>("msoftdrop")`.
- New event-level values can be attached to `event`, for example:
  - attach selected muons: `event.set("muons", selected_muons);`
  - attach corrected MET: `event.set("met_pt", corrected_met_pt);`
  - attach the reconstructed W candidate: `event.set("leptonicW", leptonic_w);`
- New object-level features can be attached to each object, for example:
  - attach corrected four-vectors: `auto corrected_p4 = polar_p4(obj); obj.set("p4", corrected_p4);`
  - attach linked subjets to a given fatjet: `fj.set("subjets", linked_subjets);`
- Channel producers are plain C++ event loops. In the main `analyze()` function, use `return false` to veto an event.
- A YAML card in `configs/run/` contains all information to guide the run.
- Corrections use modern correctionlib payloads where possible. JEC and MET corrections build on the CMSJMECalculators project.

You do not need to write this code or worry about C++ syntax; agents will fill in the implementation, and you only need to review it.

## Current Scope

The implemented channel is:

- `muon`: a heavy-flavour muon control region targeting semileptonic ttbar-like phase space, enriched in boosted top/W jets.

Main files:

- `app/nano_run.cpp`: local runner.
- `app/nano_make_condor.cpp`: Condor submission builder.
- `configs/run/`: runnable YAML cards.
- `configs/samples/`: dataset YAML files for batch submission.

For agents: for framework details, read `docs/framework-structure.md`.

## Build the Project

Use the ROOT/LCG runtime before configuring, building, or running:

```bash
source /cvmfs/sft.cern.ch/lcg/views/LCG_108/x86_64-el9-gcc13-opt/setup.sh
```

Build:

```bash
cmake -S . -B build
cmake --build build -j
```

## Process One Input

Example using a local validation file:

```bash
build/nano_run \
  --input-files /store/mc/RunIISummer20UL18NanoAODv9/TTToSemiLeptonic_TuneCP5_13TeV-powheg-pythia8/NANOAODSIM/106X_upgrade2018_realistic_v16_L1v1-v1/120000/87DEE912-70CF-A549-B10B-1A229B256E88.root \
  --output-file muon_2018_test.root \
  --config configs/run/muon_2018_v9.yaml \
  --channel muon \
  --num-events 5000
```

`--input-files` accepts one file or a comma-separated list. Local paths, `root://...` paths, and `/store/...` paths are supported.

Useful options:

```bash
--tree-name Events
--set output.include_lhe_weights=true
--variations all
```

`--variations all` writes the nominal and JME variation outputs in one event loop.

## Run Validation

See `tests/README.md`.

## Make Condor Jobs

Create a Condor work directory from a sample YAML:

```bash
build/nano_make_condor \
  --input-yaml configs/samples/muon_2018_v9_MC.yaml \
  --job-dir jobs/condor_muon_2018_v9_MC \
  --output-dir /path/to/output \
  --config configs/run/muon_2018_v9.yaml \
  --channel muon \
  --nfiles-per-job 5 \
  --num-events -1
```

This creates the requested Condor work directory, copies a merged config snapshot, packs the repository, and writes `submit.jdl`.

Submit manually:

```bash
cd jobs/condor_muon_2018_v9_MC
condor_submit submit.jdl
```

Each job runs `process.sh`, unpacks the repository, builds it if needed, prints the full `nano_run` command, and writes one ROOT piece under:

```text
<output-dir>/pieces/
```

After jobs finish, return to the repository root and merge Condor pieces with:

```bash
build/nano_merge /path/to/output
```

Pass the base output directory, not the `pieces/` subdirectory. `nano_merge` reads input pieces from:

```text
<output-dir>/pieces/
```

It first writes merged ROOT files to a temporary directory, then copies all merged outputs back under:

```text
<output-dir>/
```

## Adding Channels

Follow `docs/create-new-channel.md`.

The intended workflow is that you define the physics purpose and review the logic, while agents help write a new channel by following the existing producer pattern.
