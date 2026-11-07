#include "utils/ascii_table.h"
#include <sstream>
#include <algorithm>
#include <numeric>
#include <cstdint>

namespace isdbt {

static uint32_t decode_utf8(const std::string& str, size_t index, size_t& char_len) {
    unsigned char c0 = static_cast<unsigned char>(str[index]);
    if (c0 < 0x80) {
        char_len = 1;
        return c0;
    }
    if ((c0 & 0xE0) == 0xC0 && index + 1 < str.size()) {
        char_len = 2;
        unsigned char c1 = static_cast<unsigned char>(str[index + 1]);
        return ((c0 & 0x1F) << 6) | (c1 & 0x3F);
    }
    if ((c0 & 0xF0) == 0xE0 && index + 2 < str.size()) {
        char_len = 3;
        unsigned char c1 = static_cast<unsigned char>(str[index + 1]);
        unsigned char c2 = static_cast<unsigned char>(str[index + 2]);
        return ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
    }
    if ((c0 & 0xF8) == 0xF0 && index + 3 < str.size()) {
        char_len = 4;
        unsigned char c1 = static_cast<unsigned char>(str[index + 1]);
        unsigned char c2 = static_cast<unsigned char>(str[index + 2]);
        unsigned char c3 = static_cast<unsigned char>(str[index + 3]);
        return ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    }
    char_len = 1;
    return c0;
}

static int codepoint_width(uint32_t cp) {
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) {
        return 0; // Controles
    }
    if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0xFE00 && cp <= 0xFE0F)) {
        return 0; // Diacríticos combinantes ou zero-width
    }
    // Caracteres CJK e Emojis largos (largura 2)
    if ((cp >= 0x1100 && cp <= 0x115F) ||
        (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE10 && cp <= 0xFE19) ||
        (cp >= 0xFE30 && cp <= 0xFE6F) ||
        (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1F300 && cp <= 0x1F64F) ||
        (cp >= 0x1F900 && cp <= 0x1F9FF) ||
        (cp >= 0x20000 && cp <= 0x3FFFD)) {
        return 2;
    }
    return 1;
}

size_t AsciiTable::utf8_display_width(const std::string& str) {
    size_t width = 0;
    size_t i = 0;
    while (i < str.size()) {
        if (static_cast<unsigned char>(str[i]) == 0x1B && i + 1 < str.size() && str[i + 1] == '[') {
            size_t j = i + 2;
            while (j < str.size() && (str[j] < 0x40 || str[j] > 0x7E)) {
                j++;
            }
            if (j < str.size()) j++;
            i = j;
            continue;
        }
        size_t char_len = 0;
        uint32_t cp = decode_utf8(str, i, char_len);
        width += codepoint_width(cp);
        i += char_len;
    }
    return width;
}

std::string AsciiTable::utf8_trim_to_width(const std::string& str, size_t max_width) {
    if (utf8_display_width(str) <= max_width) {
        return str;
    }
    size_t w = 0;
    size_t byte_pos = 0;
    while (byte_pos < str.size()) {
        size_t char_len = 0;
        uint32_t cp = decode_utf8(str, byte_pos, char_len);
        int cw = codepoint_width(cp);
        if (w + cw > max_width) {
            break;
        }
        w += cw;
        byte_pos += char_len;
    }
    return str.substr(0, byte_pos);
}

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

void AsciiTable::set_column_widths(const std::vector<size_t>& widths) {
    fixed_column_widths_ = widths;
}

size_t AsciiTable::total_width() const {
    size_t num_cols = headers_.size();
    for (const auto& r : rows_) {
        num_cols = std::max(num_cols, r.size());
    }
    if (!fixed_column_widths_.empty()) {
        num_cols = std::max(num_cols, fixed_column_widths_.size());
    }
    if (num_cols == 0) return 0;

    size_t w = 1;
    for (size_t c = 0; c < num_cols; ++c) {
        size_t cw = (c < fixed_column_widths_.size()) ? fixed_column_widths_[c] : 1;
        w += (cw + 2) + 1;
    }
    return w;
}

void AsciiTable::add_row(const std::vector<std::string>& row) {
    rows_.push_back(row);
    is_separator_row_.push_back(false);
}

void AsciiTable::add_separator() {
    rows_.push_back({});
    is_separator_row_.push_back(true);
}

void AsciiTable::clear_rows() {
    rows_.clear();
    is_separator_row_.clear();
    selected_row_ = -1;
    visible_start_ = 0;
    visible_count_ = 0;
}

void AsciiTable::set_selected_row(int index) {
    selected_row_ = index;
}

void AsciiTable::set_visible_range(size_t start, size_t count) {
    visible_start_ = start;
    visible_count_ = count;
}

std::string AsciiTable::pad(const std::string& str, size_t width, Alignment align) {
    std::string text = str;
    size_t vis_w = utf8_display_width(text);
    if (vis_w > width) {
        text = utf8_trim_to_width(text, width);
        vis_w = utf8_display_width(text);
    }
    size_t diff = (width >= vis_w) ? (width - vis_w) : 0;
    switch (align) {
        case Alignment::Right:
            return std::string(diff, ' ') + text;
        case Alignment::Center: {
            size_t left = diff / 2;
            size_t right = diff - left;
            return std::string(left, ' ') + text + std::string(right, ' ');
        }
        case Alignment::Left:
        default:
            return text + std::string(diff, ' ');
    }
}

std::string AsciiTable::render(size_t max_total_width) const {
    size_t num_cols = headers_.size();
    for (const auto& r : rows_) {
        num_cols = std::max(num_cols, r.size());
    }
    if (!fixed_column_widths_.empty()) {
        num_cols = std::max(num_cols, fixed_column_widths_.size());
    }
    if (num_cols == 0) return "";

    std::vector<size_t> col_widths(num_cols, 0);

    if (!fixed_column_widths_.empty()) {
        for (size_t c = 0; c < num_cols; ++c) {
            col_widths[c] = (c < fixed_column_widths_.size()) ? fixed_column_widths_[c] : size_t(1);
        }
    } else {
        for (size_t c = 0; c < headers_.size(); ++c) {
            col_widths[c] = std::max(col_widths[c], utf8_display_width(headers_[c]));
        }
        for (size_t r = 0; r < rows_.size(); ++r) {
            if (is_separator_row_[r]) continue;
            for (size_t c = 0; c < rows_[r].size(); ++c) {
                col_widths[c] = std::max(col_widths[c], utf8_display_width(rows_[r][c]));
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
                size_t original_width = std::max(headers_.size() > c ? utf8_display_width(headers_[c]) : size_t(0), size_t(1));
                for (const auto& row : rows_) {
                    if (c < row.size()) original_width = std::max(original_width, utf8_display_width(row[c]));
                }
                if (col_widths[c] < original_width) {
                    ++col_widths[c];
                    ++used_width;
                }
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
    size_t start_r = 0;
    size_t end_r = rows_.size();
    if (visible_count_ > 0) {
        start_r = std::min(visible_start_, rows_.size());
        end_r = std::min(start_r + visible_count_, rows_.size());
    }

    for (size_t r = start_r; r < end_r; ++r) {
        if (is_separator_row_[r]) {
            ss << border;
            continue;
        }
        bool is_sel = (selected_row_ >= 0 && static_cast<size_t>(selected_row_) == r);
        if (is_sel) {
            ss << "\033[30;47m|";
        } else {
            ss << "|";
        }
        for (size_t c = 0; c < num_cols; ++c) {
            std::string text = (c < rows_[r].size()) ? rows_[r][c] : "";
            Alignment align = (c < alignments_.size()) ? alignments_[c] : Alignment::Left;
            ss << " " << pad(text, col_widths[c], align) << " |";
        }
        if (is_sel) {
            ss << "\033[0m\n";
        } else {
            ss << "\n";
        }
    }

    // Bottom border
    ss << border;
    return ss.str();
}

void AsciiTable::print(std::ostream& os, size_t max_total_width) const {
    os << render(max_total_width);
}

} // namespace isdbt
