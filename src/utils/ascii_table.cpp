#include "utils/ascii_table.h"
#include <sstream>
#include <algorithm>
#include <numeric>

namespace isdbt {

AsciiTable::AsciiTable() = default;

AsciiTable::AsciiTable(const std::vector<std::string>& headers)
    : headers_(headers) {
    alignments_.assign(headers.size(), Alignment::Left);
}

void AsciiTable::set_headers(const std::vector<std::string>& headers) {
    headers_ = headers;
    if (alignments_.size() < headers.size()) {
        alignments_.resize(headers.size(), Alignment::Left);
    }
}

void AsciiTable::set_alignments(const std::vector<Alignment>& alignments) {
    alignments_ = alignments;
}

void AsciiTable::add_row(const std::vector<std::string>& row) {
    rows_.push_back(row);
    is_separator_row_.push_back(false);
}

void AsciiTable::add_separator() {
    rows_.push_back({});
    is_separator_row_.push_back(true);
}

std::string AsciiTable::pad(const std::string& str, size_t width, Alignment align) {
    if (str.length() >= width) {
        return str.substr(0, width);
    }
    size_t diff = width - str.length();
    switch (align) {
        case Alignment::Right:
            return std::string(diff, ' ') + str;
        case Alignment::Center: {
            size_t left = diff / 2;
            size_t right = diff - left;
            return std::string(left, ' ') + str + std::string(right, ' ');
        }
        case Alignment::Left:
        default:
            return str + std::string(diff, ' ');
    }
}

std::string AsciiTable::render(size_t max_total_width) const {
    size_t num_cols = headers_.size();
    for (const auto& r : rows_) {
        num_cols = std::max(num_cols, r.size());
    }
    if (num_cols == 0) return "";

    std::vector<size_t> col_widths(num_cols, 0);
    for (size_t c = 0; c < headers_.size(); ++c) {
        col_widths[c] = std::max(col_widths[c], headers_[c].length());
    }
    for (size_t r = 0; r < rows_.size(); ++r) {
        if (is_separator_row_[r]) continue;
        for (size_t c = 0; c < rows_[r].size(); ++c) {
            col_widths[c] = std::max(col_widths[c], rows_[r][c].length());
        }
    }

    // Garante espacamento interno minimo (+ 2 caracteres de padding por celula)
    for (size_t c = 0; c < num_cols; ++c) {
        col_widths[c] = std::max(col_widths[c], size_t(1));
    }

    const size_t fixed_width = 3 * num_cols + 1;
    if (max_total_width < fixed_width + num_cols) return "";

    size_t content_width = std::accumulate(col_widths.begin(), col_widths.end(), size_t(0));
    const size_t content_budget = max_total_width - fixed_width;
    if (content_width > content_budget) {
        size_t extra_width = content_width - num_cols;
        size_t extra_budget = content_budget - num_cols;
        size_t used_width = num_cols;
        for (size_t& width : col_widths) {
            width = 1 + ((width - 1) * extra_budget) / extra_width;
            used_width += width - 1;
        }

        for (size_t c = 0; used_width < content_budget; c = (c + 1) % num_cols) {
            size_t original_width = std::max(headers_.size() > c ? headers_[c].length() : size_t(0), size_t(1));
            for (const auto& row : rows_) {
                if (c < row.size()) original_width = std::max(original_width, row[c].length());
            }
            if (col_widths[c] < original_width) {
                ++col_widths[c];
                ++used_width;
            }
        }
    }

    // Calcula borda horizontal: +---+---+
    auto build_border = [&]() -> std::string {
        std::string border = "+";
        for (size_t c = 0; c < num_cols; ++c) {
            border += std::string(col_widths[c] + 2, '-') + "+";
        }
        border += "\n";
        return border;
    };

    std::ostringstream ss;
    std::string border = build_border();

    // Top border
    ss << border;

    // Header row
    if (!headers_.empty()) {
        ss << "|";
        for (size_t c = 0; c < num_cols; ++c) {
            std::string text = (c < headers_.size()) ? headers_[c] : "";
            Alignment align = (c < alignments_.size()) ? alignments_[c] : Alignment::Left;
            ss << " " << pad(text, col_widths[c], align) << " |";
        }
        ss << "\n";
        ss << border;
    }

    // Data rows
    for (size_t r = 0; r < rows_.size(); ++r) {
        if (is_separator_row_[r]) {
            ss << border;
            continue;
        }
        ss << "|";
        for (size_t c = 0; c < num_cols; ++c) {
            std::string text = (c < rows_[r].size()) ? rows_[r][c] : "";
            Alignment align = (c < alignments_.size()) ? alignments_[c] : Alignment::Left;
            ss << " " << pad(text, col_widths[c], align) << " |";
        }
        ss << "\n";
    }

    // Bottom border
    ss << border;
    return ss.str();
}

void AsciiTable::print(std::ostream& os, size_t max_total_width) const {
    os << render(max_total_width);
}

} // namespace isdbt
