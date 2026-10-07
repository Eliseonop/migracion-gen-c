#pragma once

// Generación de ids de clips, pistas y marcadores.
//
// Las operaciones nunca generan ids por su cuenta: piden a un IdSource. En la app
// es aleatorio (como uuid4 en video-yt); en los tests y en la paridad es una
// secuencia fija, para que una operación dé SIEMPRE el mismo resultado.

#include <cstdint>
#include <random>
#include <string>
#include <string_view>

namespace gc::core {

class IdSource {
public:
    virtual ~IdSource() = default;
    // 32 dígitos hexadecimales en minúscula, como uuid.uuid4().hex.
    [[nodiscard]] virtual std::string hex32() = 0;
};

// Ids aleatorios (la app).
class RandomIdSource final : public IdSource {
public:
    RandomIdSource();
    [[nodiscard]] std::string hex32() override;

private:
    std::mt19937_64 rng_;
};

// Secuencia determinista: la n-ésima llamada devuelve "{n:06x}ab" + 24 ceros.
// tools/fixtures/export_fixtures.py sustituye uuid.uuid4 de video-yt por la misma
// secuencia, así que los ids generados coinciden con los de Python.
class SequentialIdSource final : public IdSource {
public:
    explicit SequentialIdSource(std::uint64_t first = 1) : next_(first) {}
    [[nodiscard]] std::string hex32() override;

private:
    std::uint64_t next_;
};

// f"{prefix}{uuid4().hex[:8]}" de timeline_ops._uid.
[[nodiscard]] std::string make_uid(IdSource& ids, std::string_view prefix);

} // namespace gc::core
