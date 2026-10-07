#include "gc/core/ids.hpp"

#include <chrono>
#include <format>

namespace gc::core {

RandomIdSource::RandomIdSource() {
    std::random_device rd;
    const auto now = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::seed_seq seq{rd(), rd(), rd(), rd(), static_cast<unsigned>(now), static_cast<unsigned>(now >> 32)};
    rng_.seed(seq);
}

std::string RandomIdSource::hex32() { return std::format("{:016x}{:016x}", rng_(), rng_()); }

std::string SequentialIdSource::hex32() { return std::format("{:06x}ab{:024}", next_++, 0); }

std::string make_uid(IdSource& ids, std::string_view prefix) {
    std::string out(prefix);
    out += ids.hex32().substr(0, 8);
    return out;
}

} // namespace gc::core
