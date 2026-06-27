# NanoHRTTools C++ Framework Plan

## Current scope

The framework currently covers the execution path of:

- `HeavyFlavMuonSampleProducer`
- `HeavyFlavMinimalProducer`
- the shared `HeavyFlavBaseProducer` services they call

Implemented pieces:

- explicit NanoAOD branch list
- automatic object/attribute grouping from branch names
- `TTreeReader`-based input layer
- `Event` object with event-level attachments
- `Collection` / `ObjectView` object access layer
- dynamic per-object attributes via `set("name", value)` and `extra<T>("name")`
- output model for branch declaration and filling
- ROOT `TTree` output writing
- C++ `HeavyFlavBaseProducer`, `HeavyFlavMuonSampleProducer`, and `HeavyFlavMinimalProducer`
- JME corrections and propagation for AK4 jets, AK8 fatjets, subjets, and MET
- PU and top-pt weights
- fatjet gen matching

Deferred pieces:

- jet veto maps
- tagger inference
- mass regression inference beyond reading `particleNet_mass`
- more complete gen-history matching beyond the current stored fatjet matching fields

## Object model

### Branch schema

Input branches are declared explicitly through runtime card inheritance from `configs/common/read_branches_*.yaml`, plus any channel-only `read_branches` entries in the runtime card. Branch types are resolved from `configs/common/nano_branches_*.yaml`.

Grouping rule:

- branch names with a vector type and a `_` separator are interpreted as object branches
- the prefix before `_` becomes the object name
- the suffix becomes the attribute name

Examples:

- `FatJet_pt` -> object `FatJet`, attribute `pt`
- `Muon_miniPFRelIso_all` -> object `Muon`, attribute `miniPFRelIso_all`
- `Flag_goodVertices` -> event-level scalar

### Event and collections

`Event` owns:

- the current entry context
- event-level attachments such as `looseLeptons`, `ht`, `met_pt`
- per-object dynamic attributes, e.g. `fatjet.set("subjets", ...)`

`Collection(event, "FatJet")` is represented by `event.collection("FatJet")`.

Provided access patterns:

- `collection[i]`
- `obj.get<float>("pt")`
- `obj.pt()`, `obj.eta()`, `obj.phi()`, `obj.mass()`
- `obj.set("p4", lv)` then `obj.extra<LorentzVector>("p4")`

For C++, `obj.pt()` plus `get<T>("attr")` is the cleanest default. It keeps the interface short without requiring code generation for every possible branch.

## Directory layout

- `include/nano/core/`: framework-level abstractions
- `include/nano/io/`: input/output interfaces
- `include/nano/producers/`: producer interfaces
- `src/core/`: framework implementations
- `src/io/`: I/O implementations
- `src/producers/`: producer implementations
- `app/`: temporary executable entrypoints

This is intended to scale later to:

- `include/nano/io`
- `include/nano/core`
- `include/nano/physics`
- `include/nano/producers`

if the codebase grows.

## Recommended next steps

1. Keep channel-specific branch manifests in runtime YAML so each producer only requests the fields it needs.
2. Add validation coverage for non-muon channels, starting with `minimal`.
3. Add typed helper wrappers for common objects:
   - `MuonView`
   - `JetView`
   - `FatJetView`
4. Implement full `load_gen_history()` and matching variables from the Python logic.
5. Implement jet veto maps.
6. Move any remaining campaign-dependent constants into YAML-backed producer config.
