#include "../include/Txttokenizer/Txttokenizer.h"

namespace {
    // A line opens "date time  EVENT,...": one space between date and time,
    // two before the event name. Those split tokens; any later space is part
    // of a token (unquoted emote text, for one).
    constexpr int HEADER_SPACES = 3;
}

std::vector<std::vector<std::string_view>> tokenized_segment::tokenize(const char* mmap_pointer, size_t mmap_size) {
    // For complete files, include incomplete lines at EOF
    current = mmap_pointer;
    token_start = nullptr;
    bool in_quote = false;
    int space_count = 0;

    std::vector<std::vector<std::string_view>> all_lines_tokenized;
    std::vector<std::string_view> line_tokens;
    line_tokens.reserve(40);  // Combat log lines run 30-40 tokens; skip regrowth

    const char* end = mmap_pointer + mmap_size;

    while (current < end) {
        char c = *current;

        switch (c) {
        case ' ':
            if (!in_quote) {
                space_count++;
                if (space_count <= HEADER_SPACES) {
                    finalize_token(line_tokens);
                    discard_token();
                    ++current;
                }
                else {
                    if (token_start == nullptr) begin_token();
                    append_token();
                    ++current;
                }
            }
            else {
                append_token();
                ++current;
            }
            break;

        case '"':
            in_quote = !in_quote;
            if (in_quote) {
                begin_token();
                token_start++;
            }
            else {
                finalize_token(line_tokens);
            }
            ++current;
            break;

        case ',':
            if (!in_quote) {
                finalize_token(line_tokens);
                discard_token();
                ++current;
                break;
            }
            [[fallthrough]];

        case '\n':
            if (!in_quote) {
                finalize_token(line_tokens);
                discard_token();
                if (!line_tokens.empty()) {
                    all_lines_tokenized.push_back(line_tokens);
                    line_tokens.clear();
                }
                space_count = 0;  // Reset space counter for new line
                ++current;
            }
            else {
                append_token();
                ++current;
            }
            break;

        case '\r':
        case '\t':
            if (in_quote) {
                append_token();
            }
            else {
                finalize_token(line_tokens);
                discard_token();
                ++current;
            }
            break;

        default:
            if (!token_start) begin_token();
            append_token();
            break;
        }
    }

    // For complete files, include any trailing incomplete line
    finalize_token(line_tokens);
    if (!line_tokens.empty()) all_lines_tokenized.push_back(line_tokens);

    return all_lines_tokenized;
}

void tokenized_segment::begin_token() {
    token_start = current;
}

void tokenized_segment::append_token() {
    ++current;
}

void tokenized_segment::finalize_token(std::vector<std::string_view>& line_tokens) {
    if (token_start && current > token_start) {
        line_tokens.emplace_back(token_start, current - token_start);
    }
    token_start = nullptr;
}

void tokenized_segment::discard_token() {
    token_start = nullptr;
}
