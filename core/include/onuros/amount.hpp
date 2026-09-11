#pragma once

#include "onuros/economics.hpp"
#include <string>
#include <string_view>

namespace onuros {
inline constexpr unsigned monetary_decimal_places = 8;
inline constexpr Amount atomic_units_per_coin = 100000000;

// Strict ASCII decimal input for CLI/API boundaries, not a wire encoding.
// Requires integer digits, optional '.' plus 1..8 fractional digits, no sign,
// whitespace, separators, exponent, leading integer zeros or silent rounding.
inline std::optional<Amount> parse_amount(std::string_view text) {
    // The largest Amount is 92233720368.54775807 (20 characters).
    if (text.empty() || text.size() > 20) return std::nullopt;
    const auto dot = text.find('.');
    const auto whole_digits = dot == std::string_view::npos ? text.size() : dot;
    if (whole_digits == 0 || (whole_digits > 1 && text.front() == '0'))
        return std::nullopt;
    const auto fractional_digits = dot == std::string_view::npos ? 0 : text.size()-dot-1;
    if (dot != std::string_view::npos &&
        (fractional_digits == 0 || fractional_digits > monetary_decimal_places))
        return std::nullopt;
    Amount result = 0;
    constexpr auto max = std::numeric_limits<Amount>::max();
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == dot) continue;
        const char c = text[i];
        if (c < '0' || c > '9') return std::nullopt;
        const Amount digit = c - '0';
        if (result > (max-digit)/10) return std::nullopt;
        result = result*10+digit;
    }
    for (auto digits = fractional_digits; digits < monetary_decimal_places; ++digits) {
        if (result > max/10) return std::nullopt;
        result *= 10;
    }
    return result;
}

// Fixed precision output preserves atomic units exactly, including trailing zeros.
inline std::string format_amount(Amount amount) {
    if (amount < 0) throw std::invalid_argument("negative payment amount");
    const auto whole = std::to_string(amount / atomic_units_per_coin);
    const auto fraction = std::to_string(amount % atomic_units_per_coin);
    return whole + "." + std::string(monetary_decimal_places-fraction.size(), '0') + fraction;
}
} // namespace onuros
