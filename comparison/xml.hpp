// Link the original, independently compiled gdbsupport/xml-utils.o.
#pragma once
#include <climits>
#include <stdexcept>
#include <string>
#include <vector>

extern std::string xml_escape_text(const char*);
inline std::vector<std::string> manual_escape(const std::vector<std::string>& input) {
    std::vector<std::string> output;
    for (const auto& line : input) {
        if (line.length() > INT_MAX || line.find('\0') != line.npos)
            throw std::invalid_argument("cannot represent line as GDB XML input");
        output.push_back(xml_escape_text(line.c_str()));
    }
    return output;
}
