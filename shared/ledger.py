"""Update the single status ledger from the release, coverage receipts and independent end-user evaluator."""
import json
from pathlib import Path
import sys
import mine

def read(p):return json.loads(p.read_bytes())
def ids(items): return ', '.join(sorted(set(items))) or '—'
descriptions={
'read_csv_records':'Read quoted CSV in file order, with caller-chosen header handling.',
'read_json_records':'Read a list of objects, projecting caller-selected scalar fields in order.',
'filter_records':'Keep complete rows after a typed field-to-value or field-to-field comparison.',
'order_records':'Stable multi-column ordering of complete rows, with numeric/text keys and per-key directions.',
'report_fields':'Render chosen fields or original positions as tab-separated strings.',
'derive_integer':'Append checked integer arithmetic using a chosen field or constant.',
'previous_field':'Carry the previous value within a key tuple, including an explicit presence flag.',
'running_total':'Append an inclusive checked signed total, globally or per key.',
'rank_records':'Number rows within key tuples under stable caller ordering.',
'reachable_nodes':'Return distinct forward or reverse reachable nodes from one seed, excluding the seed.',
'sqlite_records':'Read multiple SQLite fields together in an explicit order from a read-only database.',
'xml_records':'Read correlated XML elements and attributes with caller-chosen XPath selectors.',
'zip_file_records':'Read regular ZIP entries in archive order, keeping selected metadata or exact payload bytes together.',
'tree_file_records':'Read regular files below a root with metadata or exact payload bytes, ordered by path. Skip symlinks.',
'uri_records':'Preserve original URI-list entries and project parsed scheme, host or fragment information.',
'annotate_group_measure':'Attach a group minimum, maximum, lower median or conditional count to every original row.',
'match_intervals':'Find overlapping, touching or strictly containing interval pairs within each key.',
'filter_membership':'Keep or exclude rows by membership in a literal set or the values of another column.',
'first_matching_field':'Carry the first matching field from a whole group or from rows strictly preceding the current one.',
'interval_endpoints':'Expand intervals into ordered signed endpoint events, processing departures before arrivals at the same minute.',
'conditional_field':'Append a value chosen by a typed comparison. Either branch can take a field or a literal.',
'field_text_length':'Append the length of a UTF-8 field in Unicode code points.',
'graph_witnesses':'Emit source, first hop, destination and a direct-edge flag for fixed-length walks or positive transitive closure.'
}
source_names={'sqlite_records':'Arch SQLite, sqlite3_step','xml_records':'Arch pugixml, XPath evaluator',
  'zip_file_records':'Arch libarchive, ZIP reader','tree_file_records':'Arch libarchive, physical disk traversal',
  'uri_records':'Arch uriparser, URI parser, plus catalog GLib list reader'}
def main(pair,review,out,comparison_review=None,complete_replay=None):
    comparison_review=comparison_review or review
    hashes=read(pair/'reproduction.json')
    last=list(hashes)[-1]
    bundle=pair/'run-1'/last/'bundle'
    m=mine.digest(bundle/'SHA256SUMS')
    if m!=hashes[last]:raise ValueError('saved bundle changed')
    audit=read(review/'review.json')
    if audit['manifest_sha256']!=m: raise ValueError('review measured a different bundle')
    baseline=read(review/'baseline/baseline.json')
    fixed=[k for k,x in baseline['fixed'].items() if x['pass']]
    alternate=[k for k,x in baseline['alternatives'].items() if x['pass']]
    if len(baseline['fixed'])!=100:raise ValueError('baseline is incomplete')
    stages=bundle/'qualifications/shared'
    receipt=read(review/'transfer/transfer.json')
    if read(comparison_review/'review.json')['manifest_sha256']!=m:raise ValueError('binding comparison measured a different bundle')
    direct=read(comparison_review/'compare/comparison.json')
    reqs={k:(r['roles'],r['checks']) for k,r in receipt.items() if r['passed']}
    domain_names=['Inventory','Finance','Parcel dispatch','Telemetry','Publishing','ZIP archives','Project files','Bookmarks','Build scheduling','Room bookings']
    checked={}
    coverage={}
    for stage in hashes:
        data=read(stages/stage/'requests.json')
        for ident,spec in data.items():
            if ident not in reqs or reqs[ident][0]!=[spec['operations']]:raise ValueError('final recipe differs for '+ident)
            if len(spec['checks'])!=5 or not all(x['pass_'] for x in spec['checks'].values()):raise ValueError('failed intermediate request: '+ident)
        checked[stage]=data
        coverage[stage]=read(stages/stage/'receipts.json')
    all_ids={f'H{i:03}' for i in range(1,101)}
    if set(reqs)!=all_ids:raise ValueError(f'unverified requests: {sorted(all_ids-set(reqs))}')
    if not all(len(c)==5 and all(t['pass_'] for t in c.values()) for _,c in reqs.values()):raise ValueError('failed original fixture')
    report=['# Shared needs in the 100 requests','',
        f'All 100 requests have an emitted program that passed the five original fixtures. The {len(read(bundle/"catalog.json"))}-card review bundle adds {len(descriptions)} verified bricks to the 104-card first batch. The interpretations, fields, source selectors, adapter code and acceptance examples were chosen manually. The miner and evaluator replay them without a model.','',
        f'[Cumulative bundle]({bundle}) · [Both clean-build manifests]({pair}/reproduction.json) · [All 100 final programs and fixture results]({review}/transfer/transfer.json) · [Binding comparison]({comparison_review}/compare/comparison.json)','',
        f'Final SHA-256 of `SHA256SUMS` is `{m}`. The base was the [first mined bundle]({mine.BASE}) at `{mine.BASE_HASH}`. The published catalog resolved to `boilerplate-B32-FObAiR/release-101` with verification-manifest SHA-256 `8a681bd7b9a598873ce4c9cc46b9549ea4c9a1ed706e0b96b869dc88daeb2202`.','',
        '## Ready bricks','',
        'The IDs show observed use in the passing compositions. Each native client checked every selected field and source position on the named datasets.','',
        '| Brick and behavior | Upstream code | Independent datasets | Requests |','|---|---|---|---|']
    seen=set()
    for stage in hashes:
        contracts=read(stages/stage/'contracts.json')
        cases=read(stages/stage/'cases.json')
        for name,c in contracts.items():
            used=[k for k,(arms,_) in reqs.items() if any(op[0]==name for arm in arms for op in arm)]
            source=source_names.get(name,'DuckDB, '+', '.join(c['symbols']))
            if name=='read_json_records':source='DuckDB’s vendored yyjson, yyjson_read_file'
            link=stages/stage/'source.json'
            report.append(f'| **{name}**. {descriptions[name]} | [{source}]({link}) | '+', '.join([x['id'] for x in cases if x['brick']==name]+[x['id'] for x in read(mine.HERE/'tests/additional-cases.json') if x['brick']==name])+f' | {ids(used)} |')
            seen.add(name)
    if seen!=set(descriptions): raise ValueError('missing capability description')
    report+=['','## Already covered','','| Existing operation | Evidence and users |','|---|---|']
    legacy={
        'group_rows':'First batch. Partition complete rows on caller-selected composite keys',
        'aggregate_groups':'First batch. Count, distinct-count or total per group',
        'select_group_row':'First batch. Pick first/last under stable caller ordering',
        'ordered_column':'Published catalog. Project one column without reordering',
        'threshold_entries':'Published catalog. Trigger and rearm per key',
        'sort_lines':'Published catalog. Sort strings in byte order',
        'unique_lines':'Published catalog. Keep first exact occurrence',
        'reverse_order':'Published catalog. Reverse the sequence',
        'adjacent_unique':'Published catalog. Collapse adjacent repeats',
        'first_items':'Published catalog. Keep a leading prefix',
        'archive_files':'Published catalog. Read regular ZIP member paths',
        'basenames':'Published catalog. Project final filenames'}
    for name,summary in legacy.items():
        uses=[k for k,(arms,_) in reqs.items() if any(x[0]==name for arm in arms for x in arm)]
        report.append('| '+summary+' | '+ids(uses)+' |')
    report+=['','The published TSV and URI-list readers and the text and JSON writers also supply implicit planner conversions. The original opposite-side FIFO matcher returns integers. H011 needs decimal lines, so its tested composition uses row positions and the row-report writer. Single-column SQLite and XPath projections and independent ZIP sizes had narrower output contracts than the correlated readers. The candidate inventory credits the broader readers once each. [First batch and potential uses](row-ops/request-map.tsv)','',
        '## End-user verification','','| Workflow | Requests | Passing fixtures |','|---|---|---|']
    for i,domain in enumerate(domain_names):
        report.append(f'| {domain} | H{10*i+1:03}–H{10*i+10:03} | 50/50 |')
    count=sum(len(s) for s in checked.values())
    report+=['',f'{count} fixed request specifications failed to plan against the preceding bundle, passed all five fixtures after staging, and failed to plan again when a required provider was removed. The final review re-emitted and tested all 100 from the saved bundle, including {100-count} additional compositions. For multi-output requests the harness planned each role and linked the emitted roles into one CLI, then compared all output files together. The five original fixture sets were ordinary, boundary, empty, transfer and sparse. [Batch specifications](shared/programs.py) · [Additional compositions](shared/transfer.py) · [Independent evaluator](../unllm-upgrade/experiments/boilerplate/stress/end-user-100/check.py)','',
        f'The 104-card first batch passed {len(fixed)}/100 of these same fixed recipes ({ids(fixed)}). The staged bundle passed 100/100, a gain of {100-len(fixed)} under fixed interpretations. Separate first-batch recipes also passed for {ids(alternate)}. These alternative solutions prevent interpreting that gain as the number of previously impossible English requests. The main pipeline’s interpretation of English remains a separate user-run check. [Baseline plans and executions]({review}/baseline/baseline.json)' ,'',
        '## Source and build evidence','',
        '| Batch | New bricks | Independent client cases | Full requests with ablation | Clean manifest SHA-256 |','|---|---:|---:|---:|---|']
    for stage,h in hashes.items():
        report.append(f'| [{stage}]({stages/stage}/receipts.json) | {len(read(stages/stage/"contracts.json"))} | {len(coverage[stage])} | [{len(checked[stage])}]({stages/stage}/requests.json) | `{h}` |')
    units=sum(len(x) for x in coverage.values())+sum(len(x.get("additional_cases",[])) for x in direct.values())
    compare_count=sum(x['checks'] for x in direct.values())
    report+=['',f'The {units} client specifications also ran against the extracted build and a separate instrumented build. The submeter client used independent three-column readings and checked the original window evaluator. The two earlier balance examples share numeric samples with different grouping. The emitted CLIs checked ordinary, empty, invalid and missing inputs. Packaged-library comparisons passed {compare_count} client checks after compiling the same authored adapters directly against the unmodified official libraries. The comparison measures the cost of locating and rebuilding native code with provenance. It reuses the adapter and fixture authoring effort. Authoring time was unmeasured. [Per-batch timings]({comparison_review}/compare/comparison.json)','',
        'The program verifies official Arch package signatures and PKGBUILD hashes offline. For DuckDB it locates named implementations, verifies their Clang definitions, recompiles the original translation units and retains the used source and headers. The C++ adapter supplies row layout, options and SQL expressions. The DuckDB build uses the packaged SDK and recorded compiler flags. For pugixml it recompiles the signed debug source. For SQLite, libarchive and uriparser it checks declared signatures and exported symbols and uses the signed shared libraries. Instrumentation checks executed native code or calls at the packaged C API boundary on each independent dataset. [DuckDB source pins](row-ops/mined/source/arch.json) · [Format-library pins](shared/inputs/pins.json) · [Native build flags](shared/mine.py) · [Format-library build flags and dependencies](shared/input_sources.py)','',
        'The run examined 23 distinct candidate contracts. They use 19 distinct selected upstream entry points, with shared native implementations counted once per distinct caller-visible operation. A newer signed libarchive binary failed qualification because it required libcrypto.so.4, absent on this host. The released reader uses signed core/libarchive 3.8.9-1, whose dependencies resolved here. DuckDB initially rewrote a full-partition window into a join. That candidate failed source-execution verification. The qualified build disables the window-to-join rewrite and passes the coverage check.','',
        '## Reproduction and scope','',
        'Run from the brick-miner workspace with the corpus drive mounted. The command rebuilds twice, compares all eight manifests, then evaluates the final bundle and its baseline. The pair directory records the review location.','','```sh','./mine-project shared','```','',
        'To repeat only the saved-bundle checks, run `./mine-project shared review PAIR`. To regenerate this ledger after the review, run `python3 -B shared/ledger.py PAIR REVIEW SHARED-NEEDS.md` with the two printed directories.','',
        'To compile programs using the saved bundle, set `PKG_CONFIG_PATH` to its `runtime/duckdb/lib/pkgconfig` directory. Package files and source pins are stored on the corpus drive. The binaries were qualified on x86-64 Linux.','',
        'No shared need remains open in these 100 fixture-backed interpretations. General discovery of semantics from an arbitrary C++ application, natural-language interpretation quality and throughput at the 5,000-brick target were outside this run. Exact file-payload grouping materializes the requested bytes, and graph closure can grow rapidly with dense input. Only the recorded fixture sizes were measured.']
    timing_lines=["","| Batch | Source discovery and build (s) | Brick qualification (s) | Request evaluation and staging (s) | Direct binding build (s) |","|---|---:|---:|---:|---:|"]
    for stage in hashes:
        sample=direct[stage]; t=sample["kernel_route"]
        timing_lines.append(f'| {stage} | {t["acquire_and_build_seconds"]:.1f} | {t["brick_checks_seconds"]:.1f} | {t["end_user_checks_and_staging_seconds"]:.1f} | {sample["binding_build_seconds"]:.1f} |')
    timing_lines+=["","Times are wall-clock measurements on the shared host. The extracted build includes an instrumented variant and its provenance checks. The direct binding builds one uninstrumented adapter. Shared DuckDB archive validation and unpacking happen before the per-batch timer. Individual candidate authoring times were unmeasured.",""]
    report[report.index("## Reproduction and scope"):report.index("## Reproduction and scope")]=timing_lines
    if complete_replay is not None:
        if read(complete_replay/'reproduction.json')!=hashes or (complete_replay/'predecessor.json').exists():
            raise ValueError("full replay differs or resumed from a checkpoint")
        report.insert(report.index("## Ready bricks"),f'Two additional clean runs rebuilt all eight stages directly from the original 104-card input and matched every intermediate manifest. [From-scratch reproduction]({complete_replay}/reproduction.json)\n')
    out.write_text('\n'.join(report)+'\n')
if __name__=='__main__':main(Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve(),Path(sys.argv[3]),Path(sys.argv[4]).resolve() if len(sys.argv)>4 else None,Path(sys.argv[5]).resolve() if len(sys.argv)>5 else None)
