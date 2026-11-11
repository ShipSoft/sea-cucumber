// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef SHIPDISP_TESTS_TESTUTIL_H
#define SHIPDISP_TESTS_TESTUTIL_H

// =============================================================================
//  TestUtil.h -- shared helpers for the hand-rolled test executables: a
//  failure-counting check(), a summary/exit-code helper, and a per-process
//  scratch directory so tests do not write into the CWD.
// =============================================================================

#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace testutil {

inline int failures = 0;

inline void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

/// Print "<name>: OK" when everything passed; returns the process exit code.
inline int summary(const char* name) {
    if (failures == 0) std::cout << name << ": OK\n";
    return failures == 0 ? 0 : 1;
}

/// A scratch path under the system temp dir, unique per test process.
inline std::string tempPath(const std::string& filename) {
    static const std::filesystem::path dir = [] {
        auto d = std::filesystem::temp_directory_path() /
                 ("sea_cucumber_test_" + std::to_string(::getpid()));
        std::filesystem::create_directories(d);
        return d;
    }();
    return (dir / filename).string();
}

}  // namespace testutil

#endif  // SHIPDISP_TESTS_TESTUTIL_H
