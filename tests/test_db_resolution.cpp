// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
// =============================================================================
//  Checks the aegir-style .db resolution: absolute passthrough, CWD hit, and
//  $SHIPGEOMETRY_ROOT/share/geometry fallback.
// =============================================================================

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "GeoModelGeometrySource.h"
#include "TestUtil.h"

namespace fs = std::filesystem;

namespace {
void touch(const fs::path& p) {
    if (p.has_parent_path()) fs::create_directories(p.parent_path());
    std::ofstream(p) << "x";
}
}  // namespace

int main() {
    using shipdisp::ResolveGeometryDbPath;
    using testutil::check;

    // Absolute path is returned unchanged.
    check(ResolveGeometryDbPath("/nowhere/abs.db") == "/nowhere/abs.db", "absolute passthrough");

    // Bare filename existing in CWD resolves to itself.
    const std::string cwdName = "sc_test_cwd.db";
    touch(cwdName);
    check(ResolveGeometryDbPath(cwdName) == cwdName, "CWD hit");
    std::remove(cwdName.c_str());

    // Fallback under $SHIPGEOMETRY_ROOT/share/geometry.
    const fs::path root = fs::temp_directory_path() / "sc_geo_root";
    const std::string dbName = "sc_fallback.db";
    touch(root / "share" / "geometry" / dbName);
#ifdef _WIN32
    _putenv_s("SHIPGEOMETRY_ROOT", root.string().c_str());
#else
    setenv("SHIPGEOMETRY_ROOT", root.string().c_str(), 1);
#endif
    const std::string resolved = ResolveGeometryDbPath(dbName);
    check(resolved == (root / "share" / "geometry" / dbName).string(),
          "SHIPGEOMETRY_ROOT fallback");

    fs::remove_all(root);

    return testutil::summary("test_db_resolution");
}
