#pragma once
#include <vector>
#include <string_view>
#include <string>

// Splits a combat log buffer into lines of string_view tokens that point back
// into the buffer, so the buffer has to outlive the result.
class tokenized_segment {

public:
	std::vector<std::vector<std::string_view>> tokenize(const char* mmap_pointer, size_t mmap_size);

private:
	const char* token_start = nullptr;
	const char* current = nullptr;

	void begin_token();
	void append_token();
	void finalize_token(std::vector<std::string_view>& line_tokens);
	void discard_token();
};
