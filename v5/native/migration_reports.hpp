#pragma once
#include "common.hpp"

namespace v5 {
miner::J retained_binding_job(const miner::Path &registry, const std::string &target);
std::string archive_member(const miner::J &source, const std::string &name);
miner::Path arch_keyring(const miner::Path &stage);
std::string signed_build_info(const miner::J &src, const miner::Path &recipe,
                              const miner::Path &ring);
std::string package_info(const miner::J &src, const std::string &name);
miner::J runtime_files(const miner::Path &bundle, const miner::Path &base,
                       const miner::Strings &files);
miner::Strings duckdb_sandbox(const miner::Path &exe,
                              const miner::Path &directory,
                              const miner::Path &bundle);
void retain_adapter(const miner::Path &stage, const miner::Path &bundle,
                    const miner::Path &origin, const miner::J &src,
                    const miner::J &binding, const miner::J &provenance,
                    const std::string &git_adapter);
void retain_provider_abi(const miner::Path &stage, const miner::Path &bundle,
                         const miner::J &src);
std::string verified_runtime_package(const miner::Path &work,
                                     const miner::J &source);
miner::J complete_file_plan(const miner::Path &bundle, const miner::Path &out,
                            const miner::J &spec, const miner::J &expected);
miner::Path finish_file_stage(const miner::Path &work,
                              const miner::Path &package,
                              const std::string &pin, const std::string &id,
                              const std::string &schema);
miner::Path qualify_archive_names(const miner::Path &package,
                                  const std::string &pin,
                                  const miner::Path &registry,
                                  const std::string &registry_pin);
miner::Path qualify_zip_records(const miner::Path &package,
                                const std::string &pin,
                                const miner::Path &registry,
                                const std::string &registry_pin);
miner::Path qualify_tree_records(const miner::Path &package,
                                 const std::string &pin,
                                 const miner::Path &registry,
                                 const std::string &registry_pin);
miner::Path qualify_json_records(const miner::Path &package,
                                 const std::string &pin,
                                 const miner::Path &registry,
                                 const std::string &registry_pin);
miner::Path qualify_filter_extension(const miner::Path &package,
                                     const std::string &pin,
                                     const miner::Path &registry,
                                     const std::string &registry_pin);
miner::Path qualify_json_writer(const miner::Path &package,
                                const std::string &pin,
                                const miner::Path &registry,
                                const std::string &registry_pin);
miner::J sequence_writer_identities(const miner::Path &package);
miner::J file_binding_identity(const miner::J &row, const miner::Path &bundle);
miner::Path binding_header(const miner::Path &bundle, const std::string &name);
void project_binding_records(const miner::Path &stage);
void verify_binding_records(const miner::Path &stage, const miner::J &bindings);
miner::J file_contract(const miner::Path &stage, const miner::Path &package,
                       const miner::J &record, const miner::J &source,
                       const miner::Path &registry);
miner::Path seal_file_qualification(const miner::Path &work,
                                    const miner::Path &package,
                                    const std::string &pin, miner::J bindings,
                                    Json::ArrayIndex selected,
                                    const miner::J &contract);
miner::J file_qualification_tools();
miner::J path_client_environment(const miner::Path &exe, miner::J tools);
miner::Path qualify_parent_paths(const miner::Path &package,
                               const std::string &pin,
                               const miner::Path &registry,
                               const std::string &registry_pin);
void save_file_registry(const miner::Path &stage, const miner::J &source,
                        const miner::Path &registry, const std::string &pin);
miner::Path qualify_recursive_files(const miner::Path &package,
                                    const std::string &pin,
                                    const miner::Path &registry,
                                    const std::string &registry_pin);
miner::Path qualify_uri_reader(const miner::Path &package,
                               const std::string &pin,
                               const miner::Path &registry,
                               const std::string &registry_pin);
miner::Path qualify_writer(const miner::Path &package, const std::string &pin);
miner::Path qualify_tsv_writer(const miner::Path &package,
                               const std::string &pin,
                               const miner::Path &registry,
                               const std::string &registry_pin);
miner::J replay_binding_usage(const miner::Path &snapshot,
                              const std::string &pin);
miner::J rank_supplied_usage(const miner::Path &audit, const std::string &audit_pin,
                            const miner::Path &snapshot, const std::string &ledger_pin);
miner::J select_ranked_scope(const miner::Path &package, const std::string &pin,
                            const miner::J &priority, const miner::J &job);
miner::Path qualify_filter_records(const miner::Path &package,
                                   const std::string &pin,
                                   const miner::Path &registry,
                                   const std::string &registry_pin);
miner::Path qualify_report_fields(const miner::Path &package,
                                 const std::string &pin,
                                 const miner::Path &registry,
                                 const std::string &registry_pin);
miner::Path qualify_group_rows(const miner::Path &package,
                               const std::string &pin,
                               const miner::Path &registry,
                               const std::string &registry_pin);
miner::Path qualify_column(const miner::Path &package, const std::string &pin,
                            const miner::Path &registry,
                            const std::string &registry_pin);
miner::Path qualify_select_group_row(const miner::Path &package,
                                     const std::string &pin,
                                     const miner::Path &registry,
                                     const std::string &registry_pin);
miner::Path qualify_tab_records(const miner::Path &package, const std::string &pin,
                                 const miner::Path &registry,
                                 const std::string &registry_pin);
miner::Path qualify_aggregate_groups(const miner::Path &package,
                                     const std::string &pin,
                                     const miner::Path &registry,
                                     const std::string &registry_pin);
miner::Path qualify_annotate_group_measure(const miner::Path &package,
                                           const std::string &pin,
                                           const miner::Path &registry,
                                           const std::string &registry_pin);
miner::Path qualify_derive_integer(const miner::Path &package,
                                   const std::string &pin,
                                   const miner::Path &registry,
                                   const std::string &registry_pin);
miner::Path qualify_order_records(const miner::Path &package,
                                   const std::string &pin,
                                   const miner::Path &registry,
                                   const std::string &registry_pin);
miner::Path qualify_rank_records(const miner::Path &package,
                                  const std::string &pin,
                                  const miner::Path &registry,
                                  const std::string &registry_pin);
miner::Path qualify_previous_field(const miner::Path &package,
                                    const std::string &pin,
                                    const miner::Path &registry,
                                    const std::string &registry_pin);
miner::Path qualify_running_total(const miner::Path &package,
                                    const std::string &pin,
                                    const miner::Path &registry,
                                    const std::string &registry_pin);
miner::Path qualify_csv_records(const miner::Path &package,
                                 const std::string &pin,
                                 const miner::Path &registry,
                                 const std::string &registry_pin);
miner::Path qualify_uri_records(const miner::Path &package,
                                 const std::string &pin,
                                 const miner::Path &registry,
                                 const std::string &registry_pin);
miner::Path qualify_sqlite_records(const miner::Path &package,
                                    const std::string &pin,
                                    const miner::Path &registry,
                                    const std::string &registry_pin);
miner::Path qualify_xml_records(const miner::Path &package,
                                 const std::string &pin,
                                 const miner::Path &registry,
                                 const std::string &registry_pin);
miner::J file_argument_scope(const miner::J &arguments,
                              const miner::J &parameters);
miner::Path binding_identity_fixture(const miner::Path &package,
                                     const std::string &pin);
} // namespace v5
