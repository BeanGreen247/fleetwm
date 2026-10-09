#pragma once

#include <string>
#include <vector>

namespace fleetwm {

// Backup / export / import of the whole user configuration, which is a handful of toml files in
// $XDG_CONFIG_HOME/fleetwm/. The archive is a tar.gz holding fleetwm/manifest.toml plus those files. Programs pick an
// import up through the file watching they already have (files are replaced by rename, never by replacing the folder).

// Bumped when the archive layout or the meaning of a file changes in a way an older build cannot read. An archive with a
// higher number is refused; a lower one is accepted and migrated in import_config() (nothing to migrate yet).
inline constexpr int kConfigBackupFormat = 1;

// Every file the backup may carry. Anything else in the folder (editor leftovers, .tmp files) stays out, and any other
// name inside an archive is rejected on import.
const std::vector<std::string>& backup_files();

// The subset that belongs to one machine or reveals where it connects: monitor connector names and modes (outputs.toml)
// and saved server addresses and user names (fleetfm-places.toml). Left out unless asked for.
const std::vector<std::string>& machine_specific_files();

struct BackupOptions {
  bool include_machine_specific = false;
};

struct BackupResult {
  bool ok = false;
  std::string message;                // one line for the user: what happened, or why not
  std::vector<std::string> files;     // exported / imported file names
  std::vector<std::string> skipped;   // left out, with the reason in the text
  std::string backup_dir;             // import: where the previous folder was copied (empty if there was none)
};

// $XDG_CONFIG_HOME/fleetwm
std::string config_backup_dir();

// Writes `archive` (tar.gz). Files that are missing are skipped silently; files that are not valid toml are skipped and listed.
BackupResult export_config(const std::string& archive, const BackupOptions& opt = {});

// Checks the whole archive first (names, manifest, format number, every file parses as toml) and touches nothing when
// any check fails. Then copies the current folder to fleetwm.bak-YYYYMMDD-HHMMSS next to it and replaces each file.
BackupResult import_config(const std::string& archive, const BackupOptions& opt = {});

}  // namespace fleetwm
