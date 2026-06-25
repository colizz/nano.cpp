# Tests and Validation

Run all commands from the repository root after loading the same LCG runtime used for the build:

```bash
source /cvmfs/sft.cern.ch/lcg/views/LCG_108/x86_64-el9-gcc13-opt/setup.sh
cmake --build build -j
```

## Muon validation

This is the standard reference validation for the implemented muon channel.

Run it through CTest:

```bash
ctest --test-dir build -R muon_validation --output-on-failure
```

The CTest target runs:

```bash
python3 tests/muon_validation_test.py \
  --source-dir . \
  --build-dir build \
  --group muon \
  --max-input-events 5000
```

What it does:

- Runs `build/nano_run` on the configured muon validation samples.
- Covers nominal MC/data cases for Run 2 and Run 3 cards listed in `tests/muon_validation_test.py`.
- Compares the produced ROOT output against reference ROOT files under `tests/data/muon_validation/references`.
- Runs the 2016APV MC sample with an explicit comma-separated `--variations` list and compares the key JME-sensitive branches for nominal, JES, JER, and unclustered-MET variations.
- Writes the comparison report to `build/test-muon-validation/key_branch_compare_report.txt`.

What it is meant to check:

- The muon channel still runs end-to-end with the supported YAML configs.
- Required output branches are present.
- Event selection and key scalar/vector branch values remain compatible with the stored references.
- JME variation output naming and propagation remain stable for the monitored branches.
- LHE weight copying is checked for the 2016APV MC case with `output.include_lhe_weights=true`.

Some correction-dependent branches are intentionally excluded from strict value comparisons when the C++ implementation uses newer correctionlib/CMSJMECalculators behavior than the old reference production. They are still monitored for presence and reported where relevant.

## JME bundle I/O debug diagnostic

This is a manual diagnostic for the JME correction inputs and outputs. It is controlled by the environment variable:

```bash
export NANO_JME_DEBUG_BUNDLE_IO=True
```

Run a small muon job with the debug flag enabled:

```bash
NANO_JME_DEBUG_BUNDLE_IO=True build/nano_run \
  --input-files /store/mc/RunIISummer20UL18NanoAODv9/TTToSemiLeptonic_TuneCP5_13TeV-powheg-pythia8/NANOAODSIM/106X_upgrade2018_realistic_v16_L1v1-v1/120000/87DEE912-70CF-A549-B10B-1A229B256E88.root \
  --output-file build/test-jme-debug/muon_2018_debug.root \
  --config configs/run/muon_2018_v9.yaml \
  --channel muon \
  --num-events 5 \
  2> build/test-jme-debug/jme_bundle_io.log
```

Disable it with:

```bash
unset NANO_JME_DEBUG_BUNDLE_IO
```

What it does:

- Prints the event identity, `is_mc`, `rho`, and run number used by JME.
- Prints every input vector and scalar passed into each `bundle.*->produce(...)` call in `JetMETCorrector`.
- Prints the corresponding output collections for `bundle.ak4_jets`, `bundle.met`, optional `bundle.met_smeared`, `bundle.fatjet_jets`, and `bundle.subjets`.
- Prints all events processed while the environment variable is enabled, so keep `--num-events` small unless a large log is intentional.

What it is meant to check:

- NanoAOD branch mismatches are visible before they silently propagate into corrections.
- Empty vectors, dummy fallback values, wrong integer branch types, and suspicious raw factors can be spotted directly in the log.
- JEC/JER/MET correction outputs can be compared against the exact inputs passed to CMSJMECalculators.
- Problems specific to one era, NanoAOD version, or input sample can be diagnosed without changing the physics selection code.
