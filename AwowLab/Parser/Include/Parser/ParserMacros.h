#pragma once
#include "ParserDebug.h"

namespace parser {

// Opens every parse_and_return(): returns the default-constructed data when
// the line is too short. Debug builds also record a sample of the line.
#ifndef NDEBUG
#define PARSER_VALIDATE(event_name, tokens, expected_count, data) \
    if ((tokens).size() < (expected_count)) { \
        debug::recordParseFailure(event_name, \
            (tokens).size() > 3 ? (tokens)[3] : "", \
            (tokens).size(), tokens); \
        return data; \
    }
#else
#define PARSER_VALIDATE(event_name, tokens, expected_count, data) \
    if ((tokens).size() < (expected_count)) { \
        return data; \
    }
#endif

// Closes every parse_and_return(). Debug builds count the token lengths seen
// per event type.
#ifndef NDEBUG
#define PARSER_FINALIZE(event_name, tokens) \
    debug::recordTokenCount(event_name, (tokens).size())
#else
#define PARSER_FINALIZE(event_name, tokens) ((void)0)
#endif

} // namespace parser
