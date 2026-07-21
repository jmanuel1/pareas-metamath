#ifndef _PAREAS_LPG_LEXER_REGEX_PARSER_HPP
#define _PAREAS_LPG_LEXER_REGEX_PARSER_HPP

#include "pareas/lpg/lexer/regex.hpp"
#include "pareas/lpg/parser.hpp"

#include <stdexcept>
#include <cstdint>

namespace pareas::lexer {
    struct RegexParseError: std::runtime_error {
        RegexParseError(): std::runtime_error("Parse error") {}
    };

    class RegexParser {
        Parser* parser;

    public:
        RegexParser(Parser* parser);
        SharedRegexNode parse();

    private:
        SharedRegexNode alternation();
        SharedRegexNode sequence();
        SharedRegexNode maybe_repeat();
        SharedRegexNode maybe_atom();
        SharedRegexNode group();
        uint8_t escaped_char();
    };
}

#endif
