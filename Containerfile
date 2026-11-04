FROM ghcr.io/prefix-dev/pixi:0.81.0 AS build

WORKDIR /src
COPY pixi.toml pixi.lock /src

# Cache the pixi environment in a docker layer for quick local rebuilds
RUN pixi install

COPY . /src

# Cache the built executables (pixi run would work without this really)
RUN pixi run build

# TODO We need to get the geometry from somewhere, for now, it is included with
# the copy above, but that means the image building would not work in CI (unless
# the CI then downloads the geometry from somewhere). We can also download it
# like the following if ther eis a publicly available artifact somewhere..
#ADD https://github.com/ShipSoft/Geometry/actions/runs/34952846912/artifacts/10389951944 ./

# NOTE! If we want to add a cron setup to do web-data production periodically
# within the runtime image, we probably need to move the entire pixi runtime
# environment over, which is quite big. Maybe there could be a way to rather
# statically link the required binaries instead?
RUN pixi run web-data --geometry ship_geometry.db --data output.root --view views/default.toml

FROM nginxinc/nginx-unprivileged:alpine

# TODO We are missing a "interesting" manifest for an event view in this
# variant, i.e. the current variant only adds the plain geometry.
COPY --from=build /src/web /usr/share/nginx/html

# NOTE! Handy quickfix meant for usage locally when making an image with proper
# events attached. Currently deployed version was built like this.
#COPY ./web /usr/share/nginx/html

EXPOSE 8080
