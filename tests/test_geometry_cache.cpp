// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
// =============================================================================
//  Writes a geometry cache from a synthetic IGeometrySource, reads it back with
//  CachedGeometrySource, and checks the shapes, names, transforms and box
//  dimensions survive the round-trip. No .db or GeoModel needed.
// =============================================================================

#include <TGeoBBox.h>
#include <TGeoMatrix.h>
#include <TGeoShape.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "GeometryCache.h"
#include "IGeometrySource.h"
#include "TestUtil.h"

namespace {
// A provider that emits three boxes at known z positions.
class FakeSource : public shipdisp::IGeometrySource {
   public:
    std::size_t provide(const shipdisp::GeoEmit& emit) override {
        for (int i = 0; i < 3; ++i) {
            auto* box = new TGeoBBox(10.0 + i, 20.0 + i, 30.0 + i);
            TGeoHMatrix m;
            double tr[3] = {0.0, 0.0, 1000.0 * (i + 1)};
            m.SetTranslation(tr);
            emit("vol_" + std::to_string(i), box, m, i + 1);
        }
        return 3;
    }
};
}  // namespace

int main() {
    using testutil::check;
    const std::string path = testutil::tempPath("test_geocache.root");

    {
        FakeSource src;
        const std::size_t n = shipdisp::WriteGeometryCache(src, path, 0.01, "unit test");
        check(n == 3, "wrote 3 shapes");
    }

    check(shipdisp::CachedGeometrySource::isValidCache(path), "cache is recognised as valid");
    check(!shipdisp::CachedGeometrySource::isValidCache("does_not_exist.root"),
          "missing file is not a valid cache");
    check(shipdisp::CachedGeometrySource::cachedScale(path) == 0.01, "scale round-trips");
    check(std::isnan(shipdisp::CachedGeometrySource::cachedScale("does_not_exist.root")),
          "missing file has no scale");

    shipdisp::CachedGeometrySource cache(path);
    std::vector<std::string> names;
    std::vector<double> zs, dzs;
    const std::size_t n =
        cache.provide([&](const std::string& name, TGeoShape* shape, const TGeoHMatrix& g, int) {
            names.push_back(name);
            zs.push_back(g.GetTranslation()[2]);
            if (const auto* bb = dynamic_cast<const TGeoBBox*>(shape)) dzs.push_back(bb->GetDZ());
            delete shape;  // we own it in this test
        });

    check(n == 3, "replayed 3 shapes");
    check(names.size() == 3 && names[0] == "vol_0" && names[2] == "vol_2", "names round-trip");
    if (zs.size() == 3) {
        check(zs[0] == 1000.0 && zs[2] == 3000.0, "transforms round-trip");
    }
    if (dzs.size() == 3) {
        check(dzs[0] == 30.0 && dzs[2] == 32.0, "box z half-lengths round-trip");
    }

    std::remove(path.c_str());
    return testutil::summary("test_geometry_cache");
}
