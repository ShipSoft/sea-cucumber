// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
// =============================================================================
//  Checks CompileNamePattern: regex matching, the icase flag (regression for
//  the walk ignoring it), and the glob fallback ("*snd*" is not a valid regex
//  and must be translated rather than silently matching nothing).
// =============================================================================

#include <regex>
#include <string>

#include "GeoModelLoader.h"
#include "TestUtil.h"

int main() {
    using shipdisp::CompileNamePattern;
    using testutil::check;

    // Plain regex, case-insensitive by default.
    const auto re = CompileNamePattern("SND|Neutrino");
    check(std::regex_search(std::string("the_neutrino_detector"), re), "regex icase match");
    check(!std::regex_search(std::string("StrawTube"), re), "regex non-match");

    // Case-sensitive when asked.
    const auto cs = CompileNamePattern("Straw", false);
    check(std::regex_search(std::string("StrawTube"), cs), "case-sensitive match");
    check(!std::regex_search(std::string("strawtube"), cs), "case-sensitive rejects lowercase");

    // Glob fallback: a leading '*' is a regex error, so this must arrive via
    // the glob translation (* -> .*) and still match.
    const auto glob = CompileNamePattern("*snd*");
    check(std::regex_search(std::string("Volume_SND_1"), glob), "glob icase match");
    check(!std::regex_search(std::string("Volume_Straw"), glob), "glob non-match");

    // '?' matches exactly one character in glob syntax.
    const auto q = CompileNamePattern("?traw");
    check(std::regex_search(std::string("Straw"), q), "glob '?' match");

    return testutil::summary("test_name_pattern");
}
