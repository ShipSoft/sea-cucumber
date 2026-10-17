// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
// =============================================================================
//  GeometryCache.cxx -- see header.
//
//  Storage note: we do NOT persist a bare `TGeoShape*` branch. TGeoShape is an
//  abstract base, and ROOT's interpreted I/O tries to default-construct the
//  branch's declared type on read (TClass::New -> `new TGeoShape()`), which
//  fails because the class is abstract -- the concrete subtype is lost. The
//  robust, well-trodden path is to wrap each shape in a TGeoVolume (a concrete,
//  fully-dictionaried class that ROOT geometry files persist routinely). The
//  volume owns and carries its shape with the correct concrete type, so it
//  round-trips without any special handling.
// =============================================================================

#include "GeometryCache.h"

#include <TFile.h>
#include <TGeoMatrix.h>
#include <TGeoShape.h>
#include <TGeoVolume.h>
#include <TNamed.h>
#include <TParameter.h>
#include <TTree.h>

#include <cmath>

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace shipdisp {

namespace {
constexpr const char* kTreeName = "shapes";
constexpr const char* kScaleName = "scale";

// Pack a TGeoHMatrix into a row-major 4x4 (rotation 3x3 + translation), and
// back. One helper pair so the write and read layouts cannot drift.
void packMatrix(const TGeoHMatrix& g, double (&xform)[16]) {
    const Double_t* r = g.GetRotationMatrix();
    const Double_t* t = g.GetTranslation();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) xform[4 * row + col] = r[3 * row + col];
        xform[4 * row + 3] = t[row];
    }
    xform[12] = xform[13] = xform[14] = 0.0;
    xform[15] = 1.0;
}

TGeoHMatrix unpackMatrix(const double (&xform)[16]) {
    TGeoHMatrix m;
    double rot[9] = {xform[0], xform[1], xform[2], xform[4], xform[5],
                     xform[6], xform[8], xform[9], xform[10]};
    double tr[3] = {xform[3], xform[7], xform[11]};
    m.SetRotation(rot);
    m.SetTranslation(tr);
    return m;
}
}  // namespace

std::size_t WriteGeometryCache(IGeometrySource& src, const std::string& out_path, double scale,
                               const std::string& provenance) {
    std::unique_ptr<TFile> f(TFile::Open(out_path.c_str(), "RECREATE"));
    if (!f || f->IsZombie()) {
        throw std::runtime_error("GeometryCache: cannot open '" + out_path + "' for writing");
    }

    std::string name;
    std::string* namePtr = &name;
    int depth = 0;
    double xform[16] = {0};  // row-major 4x4 (rotation 3x3 + translation)
    TGeoVolume* vol = nullptr;

    TTree tree(kTreeName, "sea_cucumber display geometry cache");
    tree.Branch("name", &namePtr);
    tree.Branch("depth", &depth, "depth/I");
    tree.Branch("xform", xform, "xform[16]/D");
    tree.Branch("vol", &vol);  // concrete class -> persists cleanly

    std::size_t n = 0;
    src.provide([&](const std::string& nm, TGeoShape* sh, const TGeoHMatrix& g, int d) {
        name = nm;
        depth = d;
        packMatrix(g, xform);

        // Wrap the shape in a concrete volume so ROOT can persist it (a bare
        // TGeoShape* branch can't be read back -- the base class is abstract).
        // Null medium: the display never needs materials. Crucially we do NOT
        // delete this. The shape and volume are registered with the global
        // TGeoManager, which owns them and frees them exactly once at exit;
        // deleting here corrupts the manager's list and segfaults in
        // ~TGeoManager. Holding ~200k volumes until this one-shot tool exits is
        // fine.
        vol = new TGeoVolume(nm.c_str(), sh);
        tree.Fill();
        ++n;
    });

    tree.Write();

    const std::string title = std::string("v") + std::to_string(kGeometryCacheVersion) + " | " +
                              std::to_string(n) + " shapes | " + provenance;
    TNamed marker(kGeometryCacheTag, title.c_str());
    marker.Write();
    TParameter<double> scaleParam(kScaleName, scale);
    scaleParam.Write();

    f->Close();
    std::cout << "[GeometryCache] wrote " << n << " shapes to '" << out_path << "'\n";
    return n;
}

bool CachedGeometrySource::isValidCache(const std::string& path) {
    std::unique_ptr<TFile> f(TFile::Open(path.c_str(), "READ"));
    if (!f || f->IsZombie()) return false;
    auto* marker = dynamic_cast<TNamed*>(f->Get(kGeometryCacheTag));
    if (!marker) return false;
    const std::string title = marker->GetTitle();
    return title.rfind(std::string("v") + std::to_string(kGeometryCacheVersion) + " ", 0) == 0;
}

double CachedGeometrySource::cachedScale(const std::string& path) {
    std::unique_ptr<TFile> f(TFile::Open(path.c_str(), "READ"));
    if (!f || f->IsZombie()) return std::nan("");
    auto* p = dynamic_cast<TParameter<double>*>(f->Get(kScaleName));
    return p ? p->GetVal() : std::nan("");
}

std::size_t CachedGeometrySource::provide(const GeoEmit& emit) {
    std::unique_ptr<TFile> f(TFile::Open(path_.c_str(), "READ"));
    if (!f || f->IsZombie()) {
        throw std::runtime_error("GeometryCache: cannot open cache '" + path_ + "'");
    }
    if (!dynamic_cast<TNamed*>(f->Get(kGeometryCacheTag))) {
        throw std::runtime_error("GeometryCache: '" + path_ + "' is not a sea_cucumber cache");
    }
    auto* tree = dynamic_cast<TTree*>(f->Get(kTreeName));
    if (!tree) {
        throw std::runtime_error("GeometryCache: no 'shapes' tree in '" + path_ + "'");
    }

    std::string* name = nullptr;
    int depth = 0;
    double xform[16] = {0};
    TGeoVolume* vol = nullptr;
    tree->SetBranchAddress("name", &name);
    tree->SetBranchAddress("depth", &depth);
    tree->SetBranchAddress("xform", xform);
    tree->SetBranchAddress("vol", &vol);

    const Long64_t nEntries = tree->GetEntries();
    std::size_t n = 0;
    for (Long64_t i = 0; i < nEntries; ++i) {
        vol = nullptr;
        tree->GetEntry(i);
        if (!vol || !vol->GetShape()) continue;

        const TGeoHMatrix m = unpackMatrix(xform);

        // The callback takes ownership of the shape, so hand it a clone rather
        // than the volume-owned original.
        auto* clone = static_cast<TGeoShape*>(vol->GetShape()->Clone());
        emit(name ? *name : std::string(), clone, m, depth);
        ++n;
    }
    std::cout << "[GeometryCache] replayed " << n << " shapes from '" << path_ << "'\n";
    return n;
}

}  // namespace shipdisp
