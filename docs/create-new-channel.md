# Create a New Channel

## Goal

Add one new producer with minimal framework churn and with enough file-level documentation that a reviewer can understand the channel without reading all helper code.

## Required workflow

1. Start from the Python reference producer and list the exact event flow in order.
2. Implement channel-specific logic in `src/producers/<NewChannel>.cpp`.
3. Reuse shared services from `HeavyFlavBaseProducer` or `src/helpers/` whenever the logic is not channel-specific.
4. Add only the branches the channel reads to the runtime card's `read_branches`.
5. Add or update YAML config instead of hard-coding campaign-dependent constants.
6. Add at least one executable validation path, usually a smoke test.

## What belongs where

- Channel cuts, channel-only output branches, and channel intent: the channel producer.
- Reusable computations shared across channels: `src/helpers/`.
- Event/object utilities with no physics ownership: `src/core/`.
- Era, campaign, trigger, stored taggers, input branch manifests, b-tag, and similar run configuration: YAML.

## Header comment required in every new channel `.cpp`

At the top of the file, add a short block with:

- channel name
- physics purpose
- why this phase space is useful
- bullet list of the actual selections implemented in code

The comment should let a human reviewer answer two questions quickly:

- What sample topology is this channel trying to isolate?
- Which cuts are responsible for that topology?

## Design rules

- Keep the event selection order aligned with the Python reference.
- Prefer explicit code over clever abstractions.
- Do not request branches “just in case”.
- Do not hide channel logic inside generic helpers.
- If a constant can vary by era, nano version, or channel, put it in YAML.

## Minimum checklist before finishing

- New producer is wired into the runner.
- Required config card exists.
- Channel file has the required header comment.
- Runtime card `read_branches` matches the branches actually read.
- A smoke test or equivalent small regression runs successfully.
