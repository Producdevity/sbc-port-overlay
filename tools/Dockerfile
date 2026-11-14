# SPDX-License-Identifier: GPL-3.0-or-later
FROM debian@sha256:268fb24c39bca92d326dd83ea3abebe4a3f7751c804ffef23bb6d857dd343ff8
RUN rm -f '/etc/apt/sources.list.d/debian.sources' && \
    printf '%s\n' 'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/20250101T000000Z bookworm main' > /etc/apt/sources.list && \
    printf 'Package: *\nPin: origin snapshot.debian.org\nPin-Priority: 1001\n' > /etc/apt/preferences.d/spo-snapshot && \
    apt-get -o Acquire::Retries=2 update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --allow-downgrades --no-install-recommends \
      build-essential cmake ninja-build pkg-config libsdl2-dev \
      libegl1-mesa-dev libgles2-mesa-dev libgl1-mesa-dri && \
    dpkg-query -W > /usr/local/share/spo-build-packages.txt && \
    find '/var/lib/apt/lists' -type f -delete
WORKDIR /work
RUN apt-get -o Acquire::Retries=2 update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --allow-downgrades --no-install-recommends xvfb xauth clang-format && \
    dpkg-query -W > /usr/local/share/spo-build-packages.txt && \
    find '/var/lib/apt/lists' -type f -delete
