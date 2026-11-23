# Three C++ capabilities from official Arch packages

Three candidates yielded three distinct capabilities and three newly passing programs. The batch used the active 101-card catalog as its base and produced 104 cards and 61 components. The earlier 103-card XXH32 and XML escaping bundle has its own staging directory.

| Package and typed task | Example | Active catalog | With brick | With that brick removed |
|---|---|---|---|---|
| `core/curl 8.23.0-1`. Encode URI components, strings → strings | `a b/c?` → `a%20b%2Fc%3F` | No program | C++ client and 8/8 CLI checks passed | No program |
| `extra/libuv 1.52.1-2`. Canonicalize IPv6 literals, strings → strings | `2001:0DB8:0:0:0:0:0:1` → `2001:db8::1` | No program | C++ client and 11/11 CLI checks passed | No program |
| `extra/libsoup3 3.8.0-1`. Convert GMT HTTP dates to signed Unix seconds, strings → integers | `Sun, 06 Nov 1994 08:49:37 GMT` → `784111777` | No program | C++ client and 11/11 CLI checks passed | No program |

The input to each complete program was a file of lines, and the output was a JSON array. The typed tasks and CLI expectations were frozen before adapting the packages. The active planner reported an unknown outcome for each. Adding the outcome names alone still yielded no provider chain. With all three bricks present, the planner selected the existing line reader, the matching mined brick and the existing JSON writer. Removing only the matching brick kept the task, outcome name and other two bricks available. The planner then reported no provider chain for each task. [Before mining](../evidence/batch2/before.json) · [Frozen cases](tasks.json)

The URI client checked all 256 byte values. The IPv6 checks covered compression ties, IPv4-mapped literals and rejection of malformed or scoped addresses. The date checks covered mismatched weekdays, invalid dates and years 0001 and 9999. CLI checks also covered missing input and an unwritable output destination. [C++ clients](clients)

## Acquisition and reproducibility

The package and behavior choices, acceptance cases, API signature filters and adapter templates were written manually. The curl template handles byte lengths and ownership. The libuv template validates and converts address literals. The libsoup template enforces canonical dates and uses the 400-year Gregorian cycle to support the full four-digit year range. Each run verifies the signed Arch packages, matches installed files to their archive bytes, discovers the API declarations with Clang, fills the templates and runs qualification without a model. The libsoup route also verifies ownership against the packaged GIR. The package archives and headers are byte-pinned. Upstream release references are recorded by name. [Package pins](sources.json) · [Templates](adapters)

Across the two clean runs, automated acquisition took 0.32–0.65 seconds per candidate and qualification took 3.82–4.66 seconds per candidate. Both runs used the first pilot's planner harness. Manual preparation time was unmeasured. All three selected candidates qualified, with zero rejections or duplicates.

## Review and rerun

Both saved bundles and both fixture runs verified all 57 payload hashes. The SHA-256 of `SHA256SUMS` matched across all four runs.

```text
c277ef2590f1918966defc4ce2eba059ea912bc739b34e30d36f7de5838bc200
```

The baseline resolved to `boilerplate-B32-FObAiR/release-101`. Its `verification.json` SHA-256 was `8a681bd7b9a598873ce4c9cc46b9549ea4c9a1ed706e0b96b869dc88daeb2202`. [First fixture run](../evidence/batch2/pair-twyhcrrj/run-1.json) · [Clean repeat](../evidence/batch2/pair-twyhcrrj/run-2.json) · [Payload hashes](../evidence/batch2/pair-twyhcrrj/SHA256SUMS)

The review bundle is at `/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/batch2-0z7miw76/run-1/bundle`. The clean repeat reproduced the same 104 cards, 61 components and payload manifest. [Saved bundle](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/batch2-0z7miw76/run-1/bundle) · [First disk run](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/batch2-0z7miw76/run-1/report.json) · [Clean disk repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/batch2-0z7miw76/run-2/report.json)

Run `./batch2/run` from the miner checkout to save another pair on the corpus drive. Run `./batch2/run --fixture` to repeat the checks in private memory and save only the evidence.
