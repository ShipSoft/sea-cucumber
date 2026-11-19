# SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
# SPDX-License-Identifier: LGPL-3.0-or-later

FROM ghcr.io/prefix-dev/pixi:0.81.0 AS build

WORKDIR /src
COPY pixi.toml pixi.lock /src

# Cache the pixi environment in a docker layer for quick local rebuilds
RUN pixi install

COPY . /src

# Cache the built executables (pixi run would work without this really)
RUN pixi run build

# The geometry has to be in the build context as ship_geometry.db; it arrives
# with the COPY above. In CI, updated-geometry.yml downloads it from the
# ShipSoft/Geometry build that triggered the run. Locally, put a .db there
# yourself. The demo event is generated against the same geometry, so the
# hits line up with the detector.
#
# NOTE! If we want to add a cron setup to do web-data production periodically
# within the runtime image, we probably need to move the entire pixi runtime
# environment over, which is quite big. Maybe there could be a way to rather
# statically link the required binaries instead?
RUN test -f ship_geometry.db \
 && pixi run demo-event --geometry ship_geometry.db \
 && pixi run web-data --geometry ship_geometry.db --data demo_events.root --view views/default.toml

FROM nginxinc/nginx-unprivileged:alpine

# TODO We are missing a "interesting" manifest for an event view in this
# variant, i.e. the current variant only adds the plain geometry.
COPY --from=build /src/web /usr/share/nginx/html

# NOTE! Handy quickfix meant for usage locally when making an image with proper
# events attached. Currently deployed version was built like this.
#COPY ./web /usr/share/nginx/html

EXPOSE 8080
