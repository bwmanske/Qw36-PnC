#ifndef PC_UTIL_H
#define PC_UTIL_H

#include <string>

namespace pc {

// ── SHA-256 ──────────────────────────────────────────────────────

/// Compute SHA-256 hex digest of a file at the given path.
/// Returns empty string on error.
std::string sha256_file(const std::string& path);

/// Compute SHA-256 hex digest of raw bytes.
std::string sha256_bytes(const uint8_t* data, size_t len);

// ── Platform data directory ──────────────────────────────────────

/// Returns the platform-specific application data directory:
///   Windows: %APPDATA%\Producer\
///   Linux:   ~/.local/share/producer/
/// Creates the directory if it does not exist.
std::string get_data_directory();

} // namespace pc

#endif // PC_UTIL_H
