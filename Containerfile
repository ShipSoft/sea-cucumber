# SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
# SPDX-License-Identifier: LGPL-3.0-or-later
FROM nginxinc/nginx-unprivileged:alpine

# TODO We are missing a "interesting" manifest for an event view in this
# variant, i.e. the current variant only adds the plain geometry.
COPY ./web /usr/share/nginx/html

EXPOSE 8080
