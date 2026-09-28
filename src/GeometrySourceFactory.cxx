// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
// =============================================================================
//  GeometrySourceFactory.cxx -- see header.
// =============================================================================

#include "GeometrySourceFactory.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

#include "GdmlGeometrySource.h"
#include "GeoModelGeometrySource.h"

namespace shipdisp {

namespace {

std::string lowerExtension(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

// Content sniffing for files whose extension says nothing useful.
bool looksLikeSqlite(const std::string& head) {
    static const char kMagic[] = "SQLite format 3";  // followed by a NUL byte
    return head.size() >= sizeof(kMagic) - 1 &&
           std::memcmp(head.data(), kMagic, sizeof(kMagic) - 1) == 0;
}

bool looksLikeGdml(const std::string& head) {
    std::size_t i = 0;
    if (head.compare(0, 3, "\xEF\xBB\xBF") == 0) i = 3;  // UTF-8 BOM
    while (i < head.size() && std::isspace(static_cast<unsigned char>(head[i]))) ++i;
    if (i >= head.size() || head[i] != '<') return false;
    // An XML prolog or comment alone is not enough; require the gdml element
    // (or, for a truncated read, at least an XML document).
    return head.find("<gdml") != std::string::npos || head.compare(i, 5, "<?xml") == 0;
}

}  // namespace

const char* GeometryFormatName(GeometryFormat f) {
    return f == GeometryFormat::Gdml ? "GDML" : "GeoModel .db";
}

GeometryFormat DetectGeometryFormat(const std::string& path) {
    const std::string ext = lowerExtension(path);
    if (ext == ".gdml") return GeometryFormat::Gdml;
    if (ext == ".db" || ext == ".sqlite" || ext == ".sqlite3") return GeometryFormat::GeoModelDb;

    std::ifstream in(path, std::ios::binary);
    if (in) {
        std::string head(4096, '\0');
        in.read(head.data(), static_cast<std::streamsize>(head.size()));
        head.resize(static_cast<std::size_t>(in.gcount()));
        if (looksLikeSqlite(head)) return GeometryFormat::GeoModelDb;
        if (looksLikeGdml(head)) return GeometryFormat::Gdml;
    }
    return GeometryFormat::GeoModelDb;  // historical default
}

std::string ResolveGeometryPath(const std::string& file) { return ResolveGeometryDbPath(file); }

std::unique_ptr<IGeometrySource> MakeGeometrySource(const std::string& file, GeoLoadOptions opt) {
    const std::string resolved = ResolveGeometryPath(file);
    if (DetectGeometryFormat(resolved) == GeometryFormat::Gdml) {
        return std::make_unique<GdmlGeometrySource>(file, std::move(opt));
    }
    return std::make_unique<GeoModelGeometrySource>(file, std::move(opt));
}

std::size_t ScanGeometry(const std::string& resolved_path, const GeoLoadOptions& opt,
                         const GeoScan& scan) {
    if (DetectGeometryFormat(resolved_path) == GeometryFormat::Gdml) {
        return ScanGdml(resolved_path, opt, scan);
    }
    return ScanGeoModelDB(resolved_path, opt, scan);
}

}  // namespace shipdisp
