#include "gc/core/error.hpp"

namespace gc {

const char* to_string(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::InvalidArgument: return "invalid_argument";
    case ErrorCode::NotFound: return "not_found";
    case ErrorCode::Parse: return "parse";
    case ErrorCode::Validation: return "validation";
    case ErrorCode::Io: return "io";
    case ErrorCode::Unsupported: return "unsupported";
    case ErrorCode::Internal: return "internal";
    }
    return "internal";
}

std::string Error::to_string() const {
    std::string out = gc::to_string(code);
    out += ": ";
    if (!path.empty()) {
        out += path;
        out += ": ";
    }
    out += message;
    return out;
}

} // namespace gc
