# NanoHRTTools C++ Framework Plan

## Current scope

This scaffold only covers the execution path of:

- `HeavyFlavMuonSampleProducer`
- the subset of `HeavyFlavBaseProducer` methods it directly calls

Implemented pieces:

- explicit NanoAOD branch list
- automatic object/attribute grouping from branch names
- `RNTupleReader`-based input layer
- `Event` object with event-level attachments
- `Collection` / `ObjectView` object access layer
- dynamic per-object attributes via `set("name", value)` and `extra<T>("name")`
- minimal output model for branch declaration and filling
- C++ `HeavyFlavBaseProducer` / `HeavyFlavMuonSampleProducer`

Deferred pieces:

- JME corrections
- jet veto maps
- tagger inference
- mass regression inference beyond reading `particleNet_mass`
- full gen-history matching
- ROOT output writing

## Object model

### Branch schema

Input branches are declared explicitly by each runtime card's `read_branches` list, with types resolved from `configs/branches/*.yaml`.

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

1. Replace the in-memory `OutputModel` with a real ROOT output writer, preferably another `RNTuple` writer.
2. Keep channel-specific branch manifests in runtime YAML so each producer only requests the fields it needs.
3. Add typed helper wrappers for common objects:
   - `MuonView`
   - `JetView`
   - `FatJetView`
4. Implement full `load_gen_history()` and matching variables from the Python logic.
5. Reintroduce JME corrections as a separate service, not inside the producer itself.
6. Add a runner config layer to map input file, ntuple name, year, and producer selection from CLI or YAML.
