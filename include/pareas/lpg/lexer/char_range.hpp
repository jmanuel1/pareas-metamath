#ifndef _PAREAS_LPG_LEXER_CHAR_RANGE_HPP
#define _PAREAS_LPG_LEXER_CHAR_RANGE_HPP

#include <cstdint>
#include <compare>

namespace pareas::lexer {
    struct CharRange {
        uint8_t min;
        uint8_t max;

        bool contains(uint8_t c) const;
        bool intersecting_or_adjacent(const CharRange& other) const;
        void merge(const CharRange& other);

        bool operator==(const CharRange&) const = default;
        std::strong_ordering operator<=>(const CharRange&) const = default;
    };
}

#endif
