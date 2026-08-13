#ifndef PC_ARCHIVE_VALIDATOR_H
#define PC_ARCHIVE_VALIDATOR_H

#include <string>

namespace pc {

class ArchiveValidator {
public:
    enum class Error { None, WrongPassword, FileError };

    struct Result {
        bool valid = false;
        Error error = Error::None;
        std::string message;
    };

    static Result validate(const std::string& path, const std::string& password);
};

} // namespace pc

#endif // PC_ARCHIVE_VALIDATOR_H
