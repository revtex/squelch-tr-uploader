FROM mcr.microsoft.com/devcontainers/cpp:1-debian-12

# Build deps for the C++ Trunk-Recorder plugin (libcurl, JSON, gtest) and a
# modern Python toolchain for the script fallback.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config \
        libcurl4-openssl-dev nlohmann-json3-dev \
        libgtest-dev libgmock-dev \
        python3 python3-venv python3-pip python3-dev \
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

# Pin a tested ruff/mypy/pytest set so contributors get the same CI
# environment locally. Pinned via the post-create hook — see post-create.sh.

USER vscode
