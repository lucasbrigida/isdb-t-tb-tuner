#pragma once

#include <string>
#include <vector>
#include <ostream>

namespace isdbt {

enum class Alignment {
    Left,
    Right,
    Center
};

class AsciiTable {
public:
    AsciiTable();
    explicit AsciiTable(const std::vector<std::string>& headers);

    void set_headers(const std::vector<std::string>& headers);
    void set_alignments(const std::vector<Alignment>& alignments);
    void set_column_widths(const std::vector<size_t>& widths);
    void add_row(const std::vector<std::string>& row);
    void add_separator();

    size_t total_width() const;
    std::string render(size_t max_total_width = 100) const;
    void print(std::ostream& os, size_t max_total_width = 100) const;

    static size_t utf8_display_width(const std::string& str);
    static std::string utf8_trim_to_width(const std::string& str, size_t max_width);
    static std::string pad(const std::string& str, size_t width, Alignment align);

private:
    std::vector<std::string> headers_;
    std::vector<Alignment> alignments_;
    std::vector<size_t> fixed_column_widths_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<bool> is_separator_row_;
};

} // namespace isdbt

