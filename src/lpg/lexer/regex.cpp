#include "pareas/lpg/lexer/regex.hpp"
#include "pareas/lpg/escape.hpp"

#include <fmt/ostream.h>
#include <iostream>

#include <bitset>
#include <limits>
#include <cassert>
#include <set>
#include <map>
#include <queue>

namespace pareas::lexer {
    SharedRegexNode emptySetNode = std::make_shared<AlternationNode>(RegexNodeSet{});
    SharedRegexNode everything_node = std::make_shared<RepeatNode>(std::make_shared<CharSetNode>(std::set<CharRange>{CharRange{0, FiniteStateAutomaton::MAX_SYM}}));

    bool compare_shared_regex_node(SharedRegexNode a, SharedRegexNode b) {
        return *a < *b;
    }

    auto RegexNode::compile(FiniteStateAutomaton& fsa, StateIndex start) const -> StateIndex {
        auto start_re = this->normalize();
        if (*start_re == EmptyNode()) {
            return start;
        }
        auto end = fsa.add_state();
        // std::vector<SharedRegexNode> refs{std::make_shared<EmptyNode>(), this->normalize()}; // hacky, I guess
        auto empty_string = std::make_shared<EmptyNode>();
        std::map<SharedRegexNode, StateIndex, SharedRegexNodeLess> regex_to_state{{start_re, start}, {empty_string, end}};
        std::queue<SharedRegexNode> queue;
        queue.push(start_re);

        while (!queue.empty()) {
            auto current = queue.front();
            queue.pop();
            for (uint8_t ch = 0; ch < FiniteStateAutomaton::MAX_SYM; ch++) {
                auto next = current->take_derivative(ch)->normalize();
                if (*next == *emptySetNode) {
                    continue;
                }
                if (!regex_to_state.contains(next)) {
                    queue.push(next);
                    auto state = fsa.add_state();
                    regex_to_state[next] = state;
                }
                fsa.add_transition(regex_to_state[current], regex_to_state[next], ch);
            }
            if (current->matches_empty()) {
                fsa.add_epsilon_transition(regex_to_state[current], end);
            }
        }
        return end;
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

    SharedRegexNode SequenceNode::take_derivative(char ch) const {
        if (this->children.size() == 0) {
            return emptySetNode;
        } else {
            RegexNodeSet alternatives;
            std::vector<SharedRegexNode> first_sub_derivative{this->children[0]->take_derivative(ch)};
            first_sub_derivative.insert(first_sub_derivative.end(), this->children.begin() + 1, this->children.end());
            alternatives.insert(std::make_shared<SequenceNode>(std::move(first_sub_derivative)));
            if (this->children[0]->matches_empty()) {
                std::vector<SharedRegexNode> rest_children;
                rest_children.insert(rest_children.end(), this->children.begin() + 1, this->children.end());
                SequenceNode rest(std::move(rest_children));
                alternatives.insert(rest.take_derivative(ch));
            }
            return std::make_shared<AlternationNode>(std::move(alternatives));
        }
    };

    SharedRegexNode SequenceNode::normalize() const {
        // TODO: nesting
        std::vector<SharedRegexNode> children;
        for (auto child : this->children) {
            auto normalized = child->normalize();
            if (*normalized == *emptySetNode) {
                return emptySetNode;
            }
            if (*normalized == EmptyNode()) {
                continue;
            }
            children.push_back(normalized);
        }
        if (children.size() == 0) {
            return std::make_shared<EmptyNode>();
        } else if (children.size() == 1) {
            return children[0];
        }
        return std::make_shared<SequenceNode>(std::move(children));
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

    bool AlternationNode::matches_empty() const {
        for (const auto& child : this->children) {
            if (child->matches_empty())
                return true;
        }

        return false;
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

    SharedRegexNode AlternationNode::take_derivative(char ch) const {
        RegexNodeSet alternatives;
        for (auto child : this->children) {
            alternatives.insert(child->take_derivative(ch));
        }
        return std::make_shared<AlternationNode>(std::move(alternatives));
    }

    SharedRegexNode AlternationNode::normalize() const {
        // TODO: nesting
        RegexNodeSet children;
        std::set<CharRange> char_sets;
        for (auto child : this->children) {
            const auto normalized = child->normalize();
            if (*normalized == *everything_node) {
                return everything_node;
            } else if (*normalized == *emptySetNode) {
                continue;
            }
            const auto* char_set = dynamic_cast<const CharSetNode*>(normalized.get());
            if (char_set) {
                assert(!char_set->inverted);
                char_sets.insert(char_set->ranges.begin(), char_set->ranges.end());
                continue;
            }
            const auto* char_node = dynamic_cast<const CharNode*>(normalized.get());
            if (char_node) {
                char_sets.insert(CharRange{char_node->c, char_node->c});
                continue;
            }
            children.insert(normalized);
        }
        auto char_set = CharSetNode(std::move(char_sets)).normalize();
        if (*char_set != *emptySetNode) {
            children.insert(char_set);
        }
        if (children.size() == 1) {
            return *children.begin();
        }
        return std::make_shared<AlternationNode>(std::move(children));
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

    SharedRegexNode RepeatNode::take_derivative(char ch) const {
        switch (this->repeat_type) {
            case RepeatType::ZERO_OR_ONE:
                return AlternationNode(RegexNodeSet{std::make_shared<EmptyNode>(), this->child}).take_derivative(ch);
            case RepeatType::ZERO_OR_MORE:
                // cannot have shared_ptr of this : RepeatNode, since it might be owned by another shared_ptr
                // doing that is undefined behavior: https://en.cppreference.com/cpp/memory/shared_ptr#:~:text=Constructing%20a%20new%20shared%5Fptr%20using%20the%20raw%20underlying%20pointer%20owned%20by%20another%20shared%5Fptr%20leads%20to%20undefined%20behavior%2E
                // thus, the copy
                // not using https://en.cppreference.com/cpp/memory/enable_shared_from_this since I think that requires knowing that this is managed by a shared_ptr
                return std::make_shared<SequenceNode>(std::vector<SharedRegexNode>{this->child->take_derivative(ch), std::make_shared<RepeatNode>(*this)});
            case RepeatType::ONE_OR_MORE:
                return SequenceNode(std::vector<SharedRegexNode>{this->child, std::make_shared<RepeatNode>(RepeatType::ZERO_OR_MORE, this->child)}).take_derivative(ch);
        }
    }

    SharedRegexNode RepeatNode::normalize() const {
        switch (this->repeat_type) {
            case RepeatType::ZERO_OR_ONE:
                return AlternationNode(RegexNodeSet{std::make_shared<EmptyNode>(), this->child}).normalize();
            case RepeatType::ZERO_OR_MORE: {
                const auto normalized = this->child->normalize();
                const auto* repeat = dynamic_cast<const RepeatNode*>(normalized.get());
                if (repeat) {
                    return normalized;
                }
                const auto* empty_string = dynamic_cast<const EmptyNode*>(normalized.get());
                if (empty_string) {
                    return normalized;
                }
                if (*normalized == *emptySetNode) {
                    return std::make_shared<EmptyNode>();
                }
                return std::make_shared<RepeatNode>(normalized);
            }
            case RepeatType::ONE_OR_MORE:
                return SequenceNode(std::vector<SharedRegexNode>{this->child, std::make_shared<RepeatNode>(RepeatType::ZERO_OR_MORE, this->child)}).normalize();
        }
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

    SharedRegexNode CharSetNode::normalize() const {
        auto bits = std::bitset<FiniteStateAutomaton::MAX_SYM + 1>();
        for (const auto [min, max] : this->ranges) {
            for (int c = min; c <= max; ++c) {
                bits.set(c);
            }
        }
        if (this->inverted) {
            bits.flip();
        }
        std::set<CharRange> ranges;
        bool in_range = false;
        uint8_t min, max;
        for (size_t c = 0; c < bits.size(); ++c) {
            if (in_range && !bits[c]) {
                max = c - 1;
                ranges.insert(CharRange{min, max});
                in_range = false;
            } else if (!in_range && bits[c]) {
                min = c;
                in_range = true;
            }
        }
        if (in_range) {
            ranges.insert(CharRange{min, FiniteStateAutomaton::MAX_SYM});
        }
        if (ranges.size() == 0) {
            return emptySetNode;
        }
        if (ranges.size() == 1 && ranges.cbegin()->min == ranges.cbegin()->max) {
            return std::make_shared<CharNode>(ranges.cbegin()->min);
        }
        return std::make_shared<CharSetNode>(std::move(ranges));
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

    bool CharSetNode::matches_empty() const {
        return false;
    }

    SharedRegexNode CharSetNode::take_derivative(char ch) const {
        for (const auto [min, max] : this->ranges) {
            if (min <= ch && ch <= max) {
                return this->inverted ? emptySetNode : std::make_shared<EmptyNode>();
            }
        }
        return this->inverted ? std::make_shared<EmptyNode>() : emptySetNode;
    }

    void CharNode::print(std::ostream& os) const {
        fmt::print(os, "{:r}", EscapeFormatter{this->c});
    }

    bool CharNode::matches_empty() const {
        return false;
    }

    std::strong_ordering CharNode::compare_structurally(const RegexNode& other) const {
        const auto& other_sequence = dynamic_cast<const CharNode&>(other);
        return this->c <=> other_sequence.c;
    }

    SharedRegexNode CharNode::take_derivative(char ch) const {
        if (ch == this->c) {
            return std::make_shared<EmptyNode>();
        }
        return emptySetNode;
    }

    SharedRegexNode CharNode::normalize() const {
        return std::make_shared<CharNode>(*this);
    }

    void EmptyNode::print(std::ostream& is) const {}

    bool EmptyNode::matches_empty() const {
        return true;
    }

    std::strong_ordering EmptyNode::compare_structurally(const RegexNode& other) const {
        return std::strong_ordering::equal;
    }

    SharedRegexNode EmptyNode::take_derivative(char ch) const {
        return emptySetNode;
    }

    SharedRegexNode EmptyNode::normalize() const {
        return std::make_shared<EmptyNode>();
    }
}
