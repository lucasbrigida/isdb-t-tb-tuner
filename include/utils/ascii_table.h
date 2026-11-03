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
    void add_row(const std::vector<std::string>& row);
    void add_separator();

    std::string render(size_t max_total_width = 100) const;
    void print(std::ostream& os, size_t max_total_width = 100) const;

private:
    std::vector<std::string> headers_;
    std::vector<Alignment> alignments_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<bool> is_separator_row_;

    static std::string pad(const std::string& str, size_t width, Alignment align);
};

} // namespace isdbt

