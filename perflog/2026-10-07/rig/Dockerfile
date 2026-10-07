FROM debian:trixie
RUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  build-essential meson ninja-build pkg-config git ca-certificates \
  libwlroots-0.18-dev wayland-protocols libwayland-dev libinput-dev libdrm-dev libxkbcommon-dev libpixman-1-dev \
  libegl1-mesa-dev libgles2-mesa-dev libpipewire-0.3-dev libpam0g-dev libsystemd-dev libjemalloc-dev \
  libcairo2-dev libpng-dev libjpeg-dev libwebp-dev libgtest-dev libgmock-dev python3 \
  && rm -rf /var/lib/apt/lists/*
RUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  dbus-bin dbus-daemon foot wtype wlrctl grim fonts-inter fonts-dejavu-core fontconfig locales xkb-data procps psmisc libgl1-mesa-dri libegl-mesa0 libgbm1 \
  && rm -rf /var/lib/apt/lists/*
RUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  pipewire pipewire-bin wireplumber wayland-protocols libwayland-bin imagemagick \
  && rm -rf /var/lib/apt/lists/*
