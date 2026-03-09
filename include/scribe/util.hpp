#pragma once

/**
 * Utilities for composing handlers:
 *   - Chain<Hs...>    — dispatches to multiple handlers in order, with optional
 *                       short-circuiting via bool-returning handlers
 *   - Filter<H, Pred> — conditionally forwards records to H based on a predicate
 *
 * Handlers used inside a Chain must accept `const Record<Payload>&`, since the
 * record is shared across all chain members and cannot be moved.
 */

#include <concepts>
#include <tuple>
#include <utility>

#include <scribe/core.hpp>

namespace scribe::util {

namespace detail {

// Invokes h.handle(r) and returns whether the chain should continue.
// Handlers returning bool control continuation; void handlers always continue.
template <typename H, typename Payload>
auto invoke_chain(H& h, const Record<Payload>& r) -> bool {
    if constexpr (requires {
                      { h.handle(r) } -> std::same_as<bool>;
                  }) {
        return h.handle(r);
    } else {
        h.handle(r);
        return true;
    }
}

}    // namespace detail

/**
 * Dispatches a record to each handler in sequence.
 *
 * If a handler returns false the chain short-circuits and subsequent handlers
 * are skipped. Handlers returning void always allow the chain to continue.
 * This makes bool-returning handlers suitable for use as filters within a chain.
 */
template <typename... Hs>
class Chain {
public:
    explicit Chain(Hs... handlers)
        : m_handlers(std::move(handlers)...) {}

    template <typename Payload>
    void handle(const Record<Payload>& r) {
        std::apply([&](auto&... hs) -> auto { (detail::invoke_chain(hs, r) && ...); }, m_handlers);
    }

private:
    std::tuple<Hs...> m_handlers;
};

template <typename... Hs>
Chain(Hs...) -> Chain<Hs...>;

/**
 * Wraps a handler with a predicate. Records are forwarded to the handler only
 * when Pred(record) returns true. Returns false when filtered, allowing an
 * enclosing Chain to short-circuit.
 *
 * Pred must be callable as: bool(const Record<Payload>&)
 */
template <typename H, typename Pred>
class Filter {
public:
    Filter(H handler, Pred pred)
        : m_handler(std::move(handler))
        , m_pred(std::move(pred)) {}

    template <typename Payload>
    auto handle(const Record<Payload>& r) -> bool {
        if (!m_pred(r)) { return false; }
        return detail::invoke_chain(m_handler, r);
    }

private:
    H    m_handler;
    Pred m_pred;
};

template <typename H, typename Pred>
Filter(H, Pred) -> Filter<H, Pred>;

}    // namespace scribe::util
