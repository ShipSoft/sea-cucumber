// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef SHIPDISP_ARGPARSE_H
#define SHIPDISP_ARGPARSE_H

// =============================================================================
//  ArgParse.h -- minimal shared command-line helpers for the sea_cucumber
//  display and tools: a required-value accessor and checked numeric parsing.
//  Both print an error and exit(2) (the tools' usage-error convention), so a
//  typo can never silently become 0 or an uncaught std::stoll exception.
// =============================================================================

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <string>
#include <system_error>

namespace shipdisp::args {

/// The value following flag argv[i]; advances i. Exits when the value is
/// missing.
inline std::string NextValue(int argc, char* argv[], int& i, const char* flag) {
    if (i + 1 >= argc) {
        std::cerr << "error: " << flag << " needs a value\n";
        std::exit(2);
    }
    return argv[++i];
}

/// Whole-string numeric parse of a flag's value. Exits on anything that is
/// not entirely a number of type T.
template <typename T>
T ParseNumber(const std::string& v, const char* flag) {
    T out{};
    const char* end = v.data() + v.size();
    auto [p, ec] = std::from_chars(v.data(), end, out);
    if (ec != std::errc{} || p != end) {
        std::cerr << "error: " << flag << " needs a numeric value (got '" << v << "')\n";
        std::exit(2);
    }
    return out;
}

}  // namespace shipdisp::args

#endif  // SHIPDISP_ARGPARSE_H
