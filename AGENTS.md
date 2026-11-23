# Brick miner

Build and run a small, deterministic C++ brick-mining pilot. Its customer is the unllm C++ program-construction pipeline in the sibling `unllm-upgrade` checkout. The immediate deliverable is a verified candidate bundle, a reproducible command, and evidence of how much useful capability the miner added. The longer-term planning target is about 5,000 verified bricks. Start with a few candidates and measure yield before scaling.

## Resume after a fresh chat

Read the [one-brick comparison](one-brick/RESULTS.md) for the latest controlled task and the [shared-needs ledger](SHARED-NEEDS.md) for the earlier 100-request run. The one-brick experiment is complete. Resume from its saved results unless the user asks for another run.

- For a one-brick functional check, run the miner, keep the emitted code untouched, and compile and test exactly what it emitted. Record the result, including a failure. Run planner admission, ablation, or the wider suite only when the requested task includes them.
- For a direct comparison, freeze one behavioral contract and one test harness. After testing the mined brick, write the independent implementation in a separate file from the contract and run the same checks. Preserve both implementations.
- Measure shared setup once, separately from both routes. Report model use by the miner executable, assistant work to operate it, and direct authoring, builds, and repairs. Break usage into uncached input, cached input, and output. If the recorded phases combine costs, say which comparison the records support.
- Identify the source code the miner extracted and any selectors, adapters, or behavior written by hand. Passing tests establishes the tested behavior. Evidence of automatic extraction comes from the miner's outputs and provenance.

## Workspace and sources

- Own the code, build files, tests and results summary in this directory. The other Codex instance owns the interpreter and the main checkout. Read its current working files for reference. They include uncommitted work.
- The current catalog is at `/run/media/whisper/UNLLM_CORPUS/unllm/boilerplate/active`. Treat the main checkout and published corpus generations as read-only. Record the resolved catalog generation and manifest hash. Use an isolated staging bundle for compilation, planner trials and publication tests.
- Keep downloaded source archives, bulk extraction and candidate bundles under `/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/`, in a fresh directory for each run. Verify the filesystem is mounted there before writing. If the enclosure is asleep, continue miner development using small test fixtures in this workspace.
- Start with official Arch packages. AUR recipes are an additional discovery source. Record the actual source archive or revision behind each package. GitHub and npm acquisition are outside the current source scope. Prefer already installed tooling. If a new system package is needed, identify the package and repository and use the user's separate-terminal install prompt.

## Start here

Read the [corpus scope and counting convention](../unllm-upgrade/docs/CORPUS_STORAGE_AND_EXPANSION_UPDATE.md), the [B30–B32 experiment log](../unllm-upgrade/docs/EXPERIMENT_LOG.md#b31-creating-and-reusing-a-brick), and the [brick-learning qualification example](../unllm-upgrade/experiments/boilerplate/stress/brick-learning/README.md). Inspect the [existing GIR adapter generator](../unllm-upgrade/experiments/boilerplate/component-trial/learning/gir_adapter.cpp) and [provider contracts](../unllm-upgrade/experiments/boilerplate/component-trial/providers/contracts.json) before writing equivalents. B30 wrapped existing library operations. B31 discovered a GLib URI reader and generated a wrapper that passed an independent client and transferred to another task. General extraction from applications is still open.

## First pilot

1. **Pick two sources.** Inventory one packaged library with a documented API and one packaged C++ application with a separately testable module. From what those sources actually offer, pick a useful capability from each. Check for equivalent behavior in the current catalog. Freeze the intended input, output, effects, error behavior and independent acceptance examples before adapting the candidate. Record a typed task that currently lacks the capability.
2. **Try both acquisition routes.** For the library, use exported API metadata or headers and generate a thin C++ adapter. For the application, use Clang LibTooling or AST matchers with the project's actual compile commands to locate an existing function, class or module. Follow its declarations, implementation files and build dependencies. Preserve a header and its companion source as one unit. Record rejections when a candidate requires application state that cannot reasonably be supplied by a separate caller.
3. **Emit a candidate the planner can consume.** Use the active bundle's card, component and provider formats. Include a stable ID, a description of the behavior, typed inputs and outputs, source or callable, dependency packages and build flags. Preserve provenance, source hashes, file-level license evidence and required notices alongside the candidate. Count a capability once across duplicate implementations. Keep unqualified discoveries separate from verified bricks.
4. **Qualify it from outside the source project.** Compile and run an independent C++ client. Check normal behavior, a boundary case and relevant failure behavior. Run the same typed task with the baseline and staged bundles through the existing planner and emitter. Verify that the generated program uses the candidate and passes the task checks. Remove or disable the candidate in a disposable copy and rerun to establish whether it unlocked the task. Hold the interpretation fixed so this trial measures the miner.
5. **Measure the economics.** Compare the automated route with a hand-written thin binding on the same contract and checks. Record one-time tooling effort separately from per-candidate acquisition, verification and human intervention. Report candidates examined, distinct capabilities accepted, rejected or duplicated, and programs newly passing. Repeat from pinned inputs in a clean directory and compare the generated brick and manifest hashes. The miner's normal discovery, extraction and qualification path must run without a generative model. Label any manual choices or edits in the reported results.

## Handoff

Leave a command that reruns the pilot, the candidate bundle path and manifest hash, and a short `RESULTS.md` in this directory. For each route, show the selected source, what was automated, what required manual work, which tests passed, and the baseline versus staged program result. Include rejected candidates and their reasons. If a route yields zero qualified bricks, report that measured outcome. The main project can import a reviewed staged bundle later.
