// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef SHIPDISP_GEOMETRYCACHE_H
#define SHIPDISP_GEOMETRYCACHE_H

// =============================================================================
//  GeometryCache.h -- decouple "produce display geometry" from "render it".
//
//  Reading the ~1M-volume GeoModel .db through GeoModelIO takes seconds and
//  happens on every launch. Following the ALICE O2 event-display design
//  (arXiv:2503.00088), we split the pipeline with a file in between: a one-off
//  pass converts the geometry into a compact "display file" holding the exact
//  shapes and transforms already resolved, and the display then loads THAT and
//  starts near-instantly. No physics/geometry interpretation happens at view
//  time.
//
//  The cache slots behind the existing IGeometrySource seam, so the display
//  core is unchanged: it still just calls provide(emit). CachedGeometrySource
//  replays the file; WriteGeometryCache captures any provider into one.
//
//  Format: a ROOT TFile with
//    * a TTree "shapes"   -- one entry per emitted volume:
//        name (std::string), depth (int), transform (16 doubles, row-major
//        TGeoHMatrix), and a TGeoVolume wrapping the shape. We store a
//        TGeoVolume, NOT a bare TGeoShape*: TGeoShape is abstract, and ROOT's
//        I/O cannot default-construct it on read, so the concrete subtype is
//        lost. TGeoVolume is concrete and carries its shape with the correct
//        type, so booleans/pcons/etc. round-trip -- the same mechanism ROOT's
//        own .root geometry files rely on.
//    * a TNamed "sea_cucumber_cache" whose title is a version+provenance
//      string, so a stale or foreign file is detected rather than mis-read.
//
//  ROOT's own format gives us ALICE's "factor 40 vs JSON" for free, and the
//  TTree is forward-compatible: new branches are ignored by older readers.
// =============================================================================

#include <cstddef>
#include <string>

#include "GeoModelLoader.h"     // GeoEmit
#include "IGeometrySource.h"

namespace shipdisp {

// Version tag written into every cache; bump when the on-disk layout changes.
inline constexpr const char* kGeometryCacheTag = "sea_cucumber_cache";
inline constexpr int kGeometryCacheVersion = 1;

// Capture everything `src` emits (at the length scale it was configured with)
// into a display cache at `out_path`. `provenance` is a free-form note stored
// in the file (e.g. the source .db path and options) for later inspection.
// Returns the number of shapes written. Throws on I/O failure.
std::size_t WriteGeometryCache(IGeometrySource& src, const std::string& out_path,
                               const std::string& provenance = "");

// An IGeometrySource that replays a display cache written by the above. Cheap
// to construct; the file is read lazily in provide().
class CachedGeometrySource : public IGeometrySource {
   public:
    explicit CachedGeometrySource(std::string cache_path)
        : path_(std::move(cache_path)) {}

    std::size_t provide(const GeoEmit& emit) override;

    // True if `path` exists, opens, and carries a compatible cache tag. Lets
    // the app fall back to the live geometry when the cache is absent/stale.
    static bool isValidCache(const std::string& path);

   private:
    std::string path_;
};

}  // namespace shipdisp

#endif  // SHIPDISP_GEOMETRYCACHE_H
