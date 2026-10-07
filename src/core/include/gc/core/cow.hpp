#pragma once

// Cow<T>: valor con copia perezosa (copy-on-write) y estructura compartida.
//
// Es lo que permite que la timeline sea un valor INMUTABLE barato de copiar:
//   - Copiar una Timeline copia punteros a los clips, no los clips: los
//     snapshots de deshacer/rehacer y los que recibe el hilo de render comparten
//     todo lo que no ha cambiado.
//   - Editar un clip (`mut()`) lo clona solo si alguien más lo está viendo, así
//     que una operación nunca altera un snapshot ya publicado.
//
// Regla de uso: solo se llama a mut() sobre una Timeline que es TUYA (una copia
// de trabajo), nunca sobre una recibida como `const Timeline&` o compartida.

#include <memory>
#include <utility>

namespace gc {

template <class T>
class Cow {
public:
    Cow() : p_(std::make_shared<T>()) {}
    Cow(T value) : p_(std::make_shared<T>(std::move(value))) {} // NOLINT(google-explicit-constructor)

    [[nodiscard]] const T& operator*() const noexcept { return *p_; }
    [[nodiscard]] const T* operator->() const noexcept { return p_.get(); }
    [[nodiscard]] const T& get() const noexcept { return *p_; }

    // Acceso de escritura: clona el valor si está compartido.
    [[nodiscard]] T& mut() {
        if (p_.use_count() != 1) {
            p_ = std::make_shared<T>(*p_);
        }
        return *p_;
    }

    // ¿Es literalmente el mismo objeto? (sin cambios entre dos snapshots)
    [[nodiscard]] bool same_object(const Cow& other) const noexcept { return p_ == other.p_; }

private:
    std::shared_ptr<T> p_;
};

} // namespace gc
