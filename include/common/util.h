#ifndef PC_UTIL_H
#define PC_UTIL_H

#include <string>
#include <cstdint>
#include <cstddef>

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

// ── Duration parsing ─────────────────────────────────────────────

/// Parse a duration string into seconds.
/// Accepts a bare number (seconds) or a number followed by
/// 's' (seconds), 'm' (minutes), or 'h' (hours).
/// Returns 0 for empty input.
int parse_duration(const std::string& s);

// ── Process priority ─────────────────────────────────────────────

/// Returns true if the given host string refers to the local machine:
/// "localhost", "::1", or any 127.0.0.0/8 loopback address (e.g. 127.0.0.1).
bool is_localhost_host(const std::string& host);

/// Lowers the current process's scheduling priority to "below normal"
/// (Windows: BELOW_NORMAL_PRIORITY_CLASS; Linux: nice +5). A localhost
/// consumer uses this to yield CPU to the producer so the producer can
/// service remote connections promptly under CPU contention.
/// Returns true on success.
bool set_process_priority_below_normal();

} // namespace pc

#endif // PC_UTIL_H
