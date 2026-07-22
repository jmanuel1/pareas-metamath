#ifndef _PAREAS_LPG_LEXER_REGEX_HPP
#define _PAREAS_LPG_LEXER_REGEX_HPP

#include "pareas/lpg/lexer/fsa.hpp"
#include "pareas/lpg/lexer/char_range.hpp"

#include <memory>
#include <vector>
#include <utility>
#include <iosfwd>
#include <cstdint>
#include <typeindex>
#include <typeinfo>
#include <set>
#include <compare>

namespace pareas::lexer {
    struct RegexNode;

    using SharedRegexNode = std::shared_ptr<RegexNode>;

    /** Lexicographic comparison. Does no normalization or consideration of equivalence beyond syntax */
    bool compare_shared_regex_node(SharedRegexNode a, SharedRegexNode b);

    struct SharedRegexNodeLess {
        bool operator()(const SharedRegexNode& lhs, const SharedRegexNode& rhs) const {
            return compare_shared_regex_node(lhs, rhs);
        }
    };

    using RegexNodeSet = std::set<SharedRegexNode, SharedRegexNodeLess>;

    struct RegexNode {
        using StateIndex = FiniteStateAutomaton::StateIndex;

        virtual void print(std::ostream& os) const = 0;
        StateIndex compile(FiniteStateAutomaton& fsa, StateIndex start) const;
        virtual bool matches_empty() const = 0;
        virtual SharedRegexNode take_derivative(char ch) const = 0;
        /** Using the approximation of equivalence defined in https://www.khoury.northeastern.edu/home/turon/re-deriv.pdf */
        virtual SharedRegexNode normalize(void) const = 0;
        auto operator<=>(const RegexNode& other) const {
            auto type_order = std::type_index(typeid(*this)) <=> std::type_index(typeid(other));
            if (type_order == 0) {
                return this->compare_structurally(other);
            }
            return type_order;
        }
        auto operator==(const RegexNode& other) const {
            return (*this <=> other) == 0;
        }

        virtual ~RegexNode() = default;

    protected:
        virtual std::strong_ordering compare_structurally(const RegexNode& other) const = 0;
    };

    struct SequenceNode: public RegexNode {
        std::vector<SharedRegexNode> children;

        SequenceNode(std::vector<SharedRegexNode>&& children):
            children(std::move(children)) {}

        void print(std::ostream& os) const override;
        bool matches_empty() const override;
        SharedRegexNode take_derivative(char ch) const override;
        SharedRegexNode normalize(void) const override;

    private:
        std::strong_ordering compare_structurally(const RegexNode& other) const override;
    };

    struct AlternationNode: public RegexNode {
        RegexNodeSet children;

        AlternationNode(RegexNodeSet&& children):
            children(std::move(children)) {}

        void print(std::ostream& os) const override;
        bool matches_empty() const override;
        SharedRegexNode take_derivative(char ch) const override;
        SharedRegexNode normalize(void) const override;
    private:
        std::strong_ordering compare_structurally(const RegexNode& other) const override;
    };

    enum class RepeatType {
        ZERO_OR_ONE,
        ZERO_OR_MORE,
        ONE_OR_MORE
    };

    struct RepeatNode: public RegexNode {
        RepeatType repeat_type;
        SharedRegexNode child;

        RepeatNode(RepeatType repeat_type, SharedRegexNode child):
            repeat_type(repeat_type), child(child) {}

        explicit RepeatNode(SharedRegexNode child):
            repeat_type(RepeatType::ZERO_OR_MORE), child(child) {}

        void print(std::ostream& os) const override;
        bool matches_empty() const override;
        SharedRegexNode take_derivative(char ch) const override;
        SharedRegexNode normalize(void) const override;

    private:
        std::strong_ordering compare_structurally(const RegexNode& other) const override;
    };

    struct CharSetNode: public RegexNode {
        std::set<CharRange> ranges;
        bool inverted;

        CharSetNode(std::set<CharRange>&& ranges, bool inverted):
            ranges(std::move(ranges)), inverted(inverted) {}

        explicit CharSetNode(std::set<CharRange>&& ranges):
            ranges(std::move(ranges)), inverted(false) {}

        void print(std::ostream& os) const override;
        bool matches_empty() const override;
        SharedRegexNode take_derivative(char ch) const override;
        SharedRegexNode normalize(void) const override;

    private:
        std::strong_ordering compare_structurally(const RegexNode& other) const override;
    };

    struct CharNode: public RegexNode {
        uint8_t c;

        CharNode(uint8_t c):
            c(c) {}

        void print(std::ostream& os) const override;
        bool matches_empty() const override;
        SharedRegexNode take_derivative(char ch) const override;
        SharedRegexNode normalize(void) const override;

    private:
        std::strong_ordering compare_structurally(const RegexNode& other) const override;
    };

    struct EmptyNode: public RegexNode {
        EmptyNode() = default;

        void print(std::ostream& os) const override;
        bool matches_empty() const override;
        SharedRegexNode take_derivative(char ch) const override;
        SharedRegexNode normalize(void) const override;

    private:
        std::strong_ordering compare_structurally(const RegexNode& other) const override;
    };

    extern SharedRegexNode emptySetNode;
    extern SharedRegexNode everything_node;
}

#endif
