#pragma once
#include "bundle.hpp"

namespace pilots {
using namespace miner;
struct Candidates {
  J tasks = array(), meta = object(), info = object(), rejected;
  Files headers;
  std::map<std::string, Files> evidence;
};
Candidates original_pilot(const Path &work);
Candidates api_batch(const Path &work);
Path historical_base();
Strings original_keys(const Path &json);
std::string first_line(const std::string &s);
void keyring(const Path &path);
std::string signature(const Path &archive, const Path &keyring);
} // namespace pilots
