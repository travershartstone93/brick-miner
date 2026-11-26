#pragma once
#include "common.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <memory>

namespace row_replay {
struct ZipMember {
  std::string path;
  unsigned size = 0;
  bool directory() const { return path.ends_with('/'); }
  bool operator==(const ZipMember &) const = default;
};

inline void write_zip_fixture(const miner::Path &file,
                              const std::vector<ZipMember> &members) {
  using miner::check;
  std::unique_ptr<archive, decltype(&archive_write_free)> writer(
      archive_write_new(), archive_write_free);
  auto *arc = writer.get();
  check(arc && archive_write_set_format_zip(arc) == ARCHIVE_OK &&
            archive_write_set_options(arc, "zip:compression=deflate") ==
                ARCHIVE_OK &&
            archive_write_open_filename(arc, file.c_str()) == ARCHIVE_OK,
        "cannot open ZIP witness");
  for (const auto &m : members) {
    std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(
        archive_entry_new(), archive_entry_free);
    check(bool(entry), "cannot allocate ZIP witness entry");
    archive_entry_set_pathname(entry.get(), m.path.c_str());
    archive_entry_set_filetype(entry.get(),
                               m.directory() ? AE_IFDIR : AE_IFREG);
    archive_entry_set_size(entry.get(), m.size);
    archive_entry_set_perm(entry.get(), m.directory() ? 0755 : 0644);
    archive_entry_set_mtime(entry.get(), 1770000000, 0);
    check(archive_write_header(arc, entry.get()) == ARCHIVE_OK,
          "ZIP header write failed");
    if (!m.directory()) {
      const std::string payload(m.size, 'x');
      check(archive_write_data(arc, payload.data(), payload.size()) ==
                static_cast<la_ssize_t>(payload.size()),
            "ZIP payload write failed");
    }
    check(archive_write_finish_entry(arc) == ARCHIVE_OK,
          "ZIP entry write failed");
  }
  check(archive_write_close(arc) == ARCHIVE_OK,
        "ZIP witness finalization failed");
}
} // namespace row_replay
