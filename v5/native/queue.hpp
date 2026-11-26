#pragma once
#include "database.hpp"
#include "pilot.hpp"
namespace v5 {
struct Job {
  std::string name, source, route, stream;
  unsigned score;
  Strings parents;
};
std::vector<Job> pilot_jobs(const J &registry);
J environment_pins();
J worker_step(const std::string &name, const Path &work, const Path &out);
Result launch(const Job &job, const Path &work, const Path &out,
              const J &limits);
void initialize_queue(const Path &work);
void check_queue_space(const Path &work, const J &limits);
J queue_status(const Path &work);
void advance(const Path &work, unsigned max_steps);
void cancel_queue(const Path &work);
} // namespace v5
