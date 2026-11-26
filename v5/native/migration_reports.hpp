#pragma once
#include "common.hpp"

namespace v5 {
miner::J retained_binding_job(const miner::Path &registry, const std::string &target);
std::string archive_member(const miner::J &source, const std::string &name);
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
miner::Path qualify_json_writer(const miner::Path &package,
                                const std::string &pin,
                                const miner::Path &registry,
                                const std::string &registry_pin);
miner::J sequence_writer_identities(const miner::Path &package);
miner::J file_binding_identity(const miner::J &row, const miner::Path &bundle);
void project_binding_records(const miner::Path &stage);
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
miner::J replay_binding_usage(const miner::Path &snapshot,
                              const std::string &pin);
miner::Path binding_identity_fixture(const miner::Path &package,
                                     const std::string &pin);
} // namespace v5
