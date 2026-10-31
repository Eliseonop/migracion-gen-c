#pragma once

// Caché LRU con tope en BYTES (no en número de elementos), como lib/lruBytes.js de video-yt: cada
// entrada declara su peso y, al pasar el tope, se descartan las usadas hace más tiempo; una entrada
// sola mayor que el tope se conserva. `purge(keep)` borra lo que ya no pertenece a nada vivo. La
// usarán la caché de fotogramas del preview (F5), las miniaturas y los proxies.
//
// No es segura entre hilos: quien la comparta, que la proteja.

#include <concepts>
#include <cstddef>
#include <functional>
#include <list>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gc::core {

// Bytes de un fotograma RGBA (4 por píxel); 0 si alguna dimensión no es positiva.
[[nodiscard]] constexpr std::size_t pixel_bytes(int width, int height) noexcept {
    return width > 0 && height > 0 ? static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4 : 0;
}

template <class K, class V, class Hash = std::hash<K>>
class LruBytes {
public:
    // Se llama al descartar una entrada (por el tope, delete, purge, clear o al cambiar su valor).
    using EvictFn = std::function<void(const K& key, const V& value)>;

    static constexpr std::size_t kDefaultMaxBytes = std::size_t{64} * 1024 * 1024;

    explicit LruBytes(std::size_t max_bytes = kDefaultMaxBytes, EvictFn on_evict = {}) :
        max_bytes_(max_bytes), on_evict_(std::move(on_evict)) {}

    // El valor (y la entrada pasa a ser la más reciente), o nullptr.
    [[nodiscard]] V* get(const K& key) {
        const auto it = index_.find(key);
        if (it == index_.end()) {
            return nullptr;
        }
        order_.splice(order_.end(), order_, it->second);
        return &it->second->value;
    }

    // El valor sin cambiar el orden, o nullptr.
    [[nodiscard]] const V* peek(const K& key) const {
        const auto it = index_.find(key);
        return it == index_.end() ? nullptr : &it->second->value;
    }

    [[nodiscard]] bool contains(const K& key) const { return index_.contains(key); }

    // Guarda `value` con su peso, como la más reciente, y recorta. Si la clave ya tenía un valor,
    // se avisa de su descarte salvo que sea igual (en JS, el mismo objeto: ===).
    V& set(const K& key, V value, std::size_t bytes) {
        if (const auto it = index_.find(key); it != index_.end()) {
            bool evicted = true;
            if constexpr (std::equality_comparable<V>) {
                evicted = !(it->second->value == value);
            }
            drop(it->second, evicted);
        }
        order_.push_back(Entry{key, std::move(value), bytes});
        auto last = std::prev(order_.end());
        index_.emplace(key, last);
        total_ += bytes;
        trim(key);
        return last->value;
    }

    // Cambia el peso de una entrada (p. ej. una que creció) y recorta; nada si no existe. No cambia
    // el orden.
    void resize(const K& key, std::size_t bytes) {
        const auto it = index_.find(key);
        if (it == index_.end()) {
            return;
        }
        total_ = total_ - it->second->bytes + bytes;
        it->second->bytes = bytes;
        trim(key);
    }

    void erase(const K& key) {
        if (const auto it = index_.find(key); it != index_.end()) {
            drop(it->second, true);
        }
    }

    // Borra, de la más antigua a la más reciente, las entradas para las que keep(clave, valor) es false.
    template <class Keep>
    void purge(Keep keep) {
        for (auto it = order_.begin(); it != order_.end();) {
            const auto next = std::next(it);
            if (!keep(std::as_const(it->key), std::as_const(it->value))) {
                drop(it, true);
            }
            it = next;
        }
    }

    void clear() {
        purge([](const K&, const V&) { return false; });
    }

    // Las claves de la más antigua a la más reciente.
    [[nodiscard]] std::vector<K> keys() const {
        std::vector<K> out;
        out.reserve(order_.size());
        for (const Entry& e : order_) {
            out.push_back(e.key);
        }
        return out;
    }

    [[nodiscard]] std::size_t size() const noexcept { return order_.size(); }
    [[nodiscard]] std::size_t bytes() const noexcept { return total_; }
    [[nodiscard]] std::size_t max_bytes() const noexcept { return max_bytes_; }

private:
    struct Entry {
        K key;
        V value;
        std::size_t bytes = 0;
    };
    using Iter = std::list<Entry>::iterator;

    void drop(Iter it, bool evicted) {
        const Entry entry = std::move(*it);
        index_.erase(entry.key);
        order_.erase(it);
        total_ -= entry.bytes;
        if (evicted && on_evict_) {
            try {
                on_evict_(entry.key, entry.value);
            } catch (...) { // NOLINT(bugprone-empty-catch): una limpieza fallida no rompe la caché, como en JS
            }
        }
    }

    void trim(const K& keep) {
        for (auto it = order_.begin(); it != order_.end() && total_ > max_bytes_;) {
            const auto next = std::next(it);
            if (!(it->key == keep)) { // una entrada sola mayor que el tope se conserva
                drop(it, true);
            }
            it = next;
        }
    }

    std::size_t max_bytes_;
    EvictFn on_evict_;
    std::list<Entry> order_; // de la más antigua a la más reciente
    std::unordered_map<K, Iter, Hash> index_;
    std::size_t total_ = 0;
};

} // namespace gc::core
