#include "pareas/lpg/lexer/regex.hpp"
#include "pareas/lpg/escape.hpp"

#include <fmt/ostream.h>
#include <iostream>

#include <bitset>
#include <limits>
#include <cassert>
#include <set>

namespace pareas::lexer {
    bool compare_shared_regex_node(SharedRegexNode a, SharedRegexNode b) {
        return *a < *b;
    }

    void SequenceNode::print(std::ostream& os) const {
        if (this->children.size() == 1) {
            this->children[0]->print(os);
        } else {
            fmt::print("(");
            for (const auto& child : this->children) {
                child->print(os);
            }
            fmt::print(")");
        }
    }

    auto SequenceNode::compile(FiniteStateAutomaton& fsa, StateIndex start) const -> StateIndex {
        StateIndex end = start;
        for (const auto& child : this->children) {
            end = child->compile(fsa, end);
        }
        return end;
    }

    bool SequenceNode::matches_empty() const {
        for (const auto& child : this->children) {
            if (!child->matches_empty())
                return false;
        }

        return true;
    }

    std::strong_ordering SequenceNode::compare_structurally(const RegexNode& other) const {
        const auto& other_sequence = dynamic_cast<const SequenceNode&>(other);
        // not really lexicographic, but it's less work
        if (this->children.size() != other_sequence.children.size()) return this->children.size() <=> other_sequence.children.size();
        for (size_t i = 0; i < this->children.size(); i++) {
            auto child_compare = *this->children[i] <=> *(other_sequence.children[i]);
            if (child_compare != 0) return child_compare;
        }
        return std::strong_ordering::equal;
    }

    void AlternationNode::print(std::ostream& os) const {
        if (this->children.size() == 1) {
            this->children.begin()->get()->print(os);
        } else {
            fmt::print("(");
            bool first = true;
            for (const auto& child : this->children) {
                if (first)
                    first = false;
                else
                    fmt::print("|");

                child->print(os);
            }
            fmt::print(")");
        }
    }

    auto AlternationNode::compile(FiniteStateAutomaton& fsa, StateIndex start) const -> StateIndex {
        if (this->children.empty())
            return start;

        auto end = fsa.add_state();
        for (const auto& child : this->children) {
            auto child_start = fsa.add_state();
            auto child_end = child->compile(fsa, child_start);
            fsa.add_epsilon_transition(start, child_start);
            fsa.add_epsilon_transition(child_end, end);
        }

        return end;
    }

    bool AlternationNode::matches_empty() const {
        for (const auto& child : this->children) {
            if (child->matches_empty())
                return true;
        }

        return this->children.size() == 0;
    }

    std::strong_ordering AlternationNode::compare_structurally(const RegexNode& other) const {
        const auto& other_sequence = dynamic_cast<const AlternationNode&>(other);
        if (this->children.size() != other_sequence.children.size()) return this->children.size() <=> other_sequence.children.size();
        auto other_it = other_sequence.children.cbegin();
        for (auto child : this->children) {
            auto other_child = *(other_it++);
            auto child_compare = *child <=> *other_child;
            if (child_compare != 0) return child_compare;
        }
        return std::strong_ordering::equal;
    }

    void RepeatNode::print(std::ostream& os) const {
        this->child->print(os);

        char c;
        switch (this->repeat_type) {
            case RepeatType::ZERO_OR_ONE:
                c = '?';
                break;
            case RepeatType::ZERO_OR_MORE:
                c = '*';
                break;
            case RepeatType::ONE_OR_MORE:
                c = '+';
                break;
        }
        fmt::print(os, "{}", c);
    }

    auto RepeatNode::compile(FiniteStateAutomaton& fsa, StateIndex start) const -> StateIndex {
        auto loop_start = fsa.add_state();
        auto loop_end = this->child->compile(fsa, loop_start);
        auto end = fsa.add_state();

        fsa.add_epsilon_transition(start, loop_start);
        fsa.add_epsilon_transition(loop_end, end);

        // For ? and *, add the epsilon-transition which bypasses the child entirely.
        if (this->repeat_type == RepeatType::ZERO_OR_ONE || this->repeat_type == RepeatType::ZERO_OR_MORE)
            fsa.add_epsilon_transition(start, end);

        // For + and *, add the epsilon-transition which loops around to the start of the loop.
        if (this->repeat_type == RepeatType::ZERO_OR_MORE || this->repeat_type == RepeatType::ONE_OR_MORE)
            fsa.add_epsilon_transition(loop_end, loop_start);

        return end;
    }

    bool RepeatNode::matches_empty() const {
        switch (this->repeat_type) {
            case RepeatType::ZERO_OR_ONE:
            case RepeatType::ZERO_OR_MORE:
                return true;
            case RepeatType::ONE_OR_MORE:
                return this->child->matches_empty();
        }
    }

    std::strong_ordering RepeatNode::compare_structurally(const RegexNode& other) const {
        const auto& other_sequence = dynamic_cast<const RepeatNode&>(other);
        if (this->repeat_type != other_sequence.repeat_type) return this->repeat_type <=> other_sequence.repeat_type;
        return *this->child <=> *(other_sequence.child);
    }


    std::strong_ordering CharSetNode::compare_structurally(const RegexNode& other) const {
        const auto& other_sequence = dynamic_cast<const CharSetNode&>(other);
        if (this->inverted != other_sequence.inverted) return this->inverted <=> other_sequence.inverted;
        if (this->ranges.size() != other_sequence.ranges.size()) return this->ranges.size() <=> other_sequence.ranges.size();
        auto other_it = other_sequence.ranges.cbegin();
        for (auto range : this->ranges) {
            auto other_range = *(other_it++);
            if (range != other_range) return range <=> other_range;
        }
        return std::strong_ordering::equal;
    }

    void CharSetNode::print(std::ostream& os) const {
        fmt::print(os, "[{}", this->inverted ? "^" : "");

        for (const auto [min, max] : this->ranges) {
            if (min == max)
                fmt::print(os, "{:r}", EscapeFormatter{min});
            else
                fmt::print(os, "{:r}-{:r}", EscapeFormatter{min}, EscapeFormatter{max});
        }

        fmt::print(os, "]");
    }

    auto CharSetNode::compile(FiniteStateAutomaton& fsa, StateIndex start) const -> StateIndex {
        auto end = fsa.add_state();

        if (this->inverted) {
            auto bits = std::bitset<FiniteStateAutomaton::MAX_SYM + 1>();

            for (const auto [min, max] : this->ranges) {
                for (int c = min; c <= max; ++c) {
                    bits.set(c);
                }
            }

            for (size_t c = 0; c < bits.size(); ++c) {
                if (!bits.test(c))
                    fsa.add_transition(start, end, c);
            }
        } else {
            for (const auto [min, max] : this->ranges) {
                for (int c = min; c <= max; ++c) {
                    fsa.add_transition(start, end, c);
                }
            }
        }

        return end;
    }

    bool CharSetNode::matches_empty() const {
        auto bits = std::bitset<FiniteStateAutomaton::MAX_SYM + 1>();

        for (const auto [min, max] : this->ranges) {
            for (int c = min; c <= max; ++c) {
                bits.set(c);
            }
        }

        if (this->inverted)
            bits.flip();

        return bits.none();
    }

    void CharNode::print(std::ostream& os) const {
        fmt::print(os, "{:r}", EscapeFormatter{this->c});
    }

    auto CharNode::compile(FiniteStateAutomaton& fsa, StateIndex start) const -> StateIndex {
        auto end = fsa.add_state();
        fsa.add_transition(start, end, this->c);
        return end;
    }

    bool CharNode::matches_empty() const {
        return false;
    }

    std::strong_ordering CharNode::compare_structurally(const RegexNode& other) const {
        const auto& other_sequence = dynamic_cast<const CharNode&>(other);
        return this->c <=> other_sequence.c;
    }

    void EmptyNode::print(std::ostream& is) const {}

    auto EmptyNode::compile(FiniteStateAutomaton& fsa, StateIndex start) const -> StateIndex {
        return start;
    }

    bool EmptyNode::matches_empty() const {
        return true;
    }

    std::strong_ordering EmptyNode::compare_structurally(const RegexNode& other) const {
        return std::strong_ordering::equal;
    }
}
