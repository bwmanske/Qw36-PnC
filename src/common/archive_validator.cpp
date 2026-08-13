#include "common/archive_validator.h"
#include <archive.h>
#include <archive_entry.h>
#include <cstring>

namespace pc {

ArchiveValidator::Result ArchiveValidator::validate(const std::string& path, const std::string& password) {
    Result res;

    struct archive* arch = archive_read_new();
    if (!arch) {
        res.error = Error::FileError;
        res.message = "Failed to create archive reader";
        return res;
    }

    archive_read_support_filter_all(arch);
    archive_read_support_format_zip(arch);
    archive_read_support_format_rar(arch);
    archive_read_support_format_7zip(arch);

    if (!password.empty()) {
        archive_read_add_passphrase(arch, password.c_str());
    }

    int code = archive_read_open_filename(arch, path.c_str(), 4096);
    if (code != ARCHIVE_OK) {
        res.error = Error::FileError;
        res.message = std::string("Cannot open archive: ") + archive_error_string(arch);
        archive_read_free(arch);
        return res;
    }

    struct archive_entry* entry = nullptr;
    code = archive_read_next_header(arch, &entry);

    if (code == ARCHIVE_OK) {
        res.valid = true;
        res.error = Error::None;
        res.message = "Password valid";
    } else if (code == ARCHIVE_RETRY) {
        res.valid = false;
        res.error = Error::WrongPassword;
        res.message = "Wrong password";
    } else {
        res.valid = false;
        res.error = Error::FileError;
        res.message = std::string("Archive error: ") + archive_error_string(arch);
    }

    archive_read_free(arch);
    return res;
}

} // namespace pc
