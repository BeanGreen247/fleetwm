// fleetwm-config: export the whole user configuration to one tar.gz and import it back (see src/common/config_backup.hpp).
#include <cstdio>
#include <cstring>
#include <string>

#include "config_backup.hpp"
#include "version.hpp"

int main(int argc, char** argv) {
  if (fleetwm::handle_info_flags(argc, argv, "fleetwm-config", "export FILE [--all] | import FILE [--all]")) return 0;
  std::string cmd, file;
  fleetwm::BackupOptions opt;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--all") == 0) opt.include_machine_specific = true;
    else if (cmd.empty()) cmd = argv[i];
    else if (file.empty()) file = argv[i];
    else cmd.clear();  // too many arguments -> usage below
  }
  if ((cmd != "export" && cmd != "import") || file.empty()) {
    std::fprintf(stderr,
                 "usage: fleetwm-config export FILE [--all]\n"
                 "       fleetwm-config import FILE [--all]\n"
                 "--all also carries the machine specific files (outputs.toml, fleetfm-places.toml).\n");
    return 2;
  }
  const fleetwm::BackupResult r = cmd == "export" ? fleetwm::export_config(file, opt) : fleetwm::import_config(file, opt);
  std::fprintf(r.ok ? stdout : stderr, "%s\n", r.message.c_str());
  for (const auto& s : r.skipped) std::fprintf(r.ok ? stdout : stderr, "  left out: %s\n", s.c_str());
  return r.ok ? 0 : 1;
}
