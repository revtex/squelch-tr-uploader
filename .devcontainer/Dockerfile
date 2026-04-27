FROM mcr.microsoft.com/devcontainers/cpp:1-debian-12

# Build deps for the C++ Trunk-Recorder plugin (libcurl, JSON, gtest).
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config \
        libcurl4-openssl-dev nlohmann-json3-dev \
        libgtest-dev libgmock-dev \
        libboost-all-dev gnuradio-dev libssl-dev \
        ripgrep jq \
        ca-certificates curl gnupg \
    && rm -rf /var/lib/apt/lists/*

# GitHub CLI (kept fresh — needed for the maintainer workflow).
RUN curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg \
        | gpg --dearmor -o /usr/share/keyrings/githubcli-archive-keyring.gpg \
    && echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
        > /etc/apt/sources.list.d/github-cli.list \
    && apt-get update && apt-get install -y --no-install-recommends gh \
    && rm -rf /var/lib/apt/lists/*

# Pin a tested toolchain for contributors via the post-create hook
# — see post-create.sh.

# Ensure /tmp is world-writable with the sticky bit and pre-create the
# X11 forwarding socket dir. Under Podman rootless the base image's /tmp
# can come up with permissions that block the non-root `vscode` user from
# creating /tmp/.X11-unix, which causes the Dev Containers helper to fail
# right after container start. Pre-creating it (as root, with 1777) keeps
# both VS Code's X11 helper and any other /tmp users happy.
RUN chmod 1777 /tmp \
    && mkdir -p /tmp/.X11-unix \
    && chmod 1777 /tmp/.X11-unix

USER vscode
