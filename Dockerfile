# Toolchain do desafio-27: g++ 14.2 + CMake 3.31.6 + Ninja 1.12.1, tudo com versão fixada.
# Nenhum código do desafio é copiado para a imagem; o projeto é montado em /work.
#
# CMake e Ninja vêm dos binários oficiais publicados no GitHub (Kitware/CMake e
# ninja-build/ninja), baixados por versão exata e conferidos por sha256 antes de
# serem instalados. Os hashes do CMake são os do arquivo cmake-3.31.6-SHA-256.txt
# da própria release; os do Ninja foram calculados sobre os .zip da release v1.12.1.
FROM gcc:14.2.0

ARG CMAKE_VERSION=3.31.6
ARG CMAKE_SHA256_X86_64=5a1133ff103c71eb5120e2cc3de922733e7d8a26a98ae716397e8676adb367bf
ARG CMAKE_SHA256_AARCH64=b4cc788d63112b2749b40627e719eb5d3b8ed8f00c36d77189f4019cfe64bc9e
ARG NINJA_VERSION=1.12.1
ARG NINJA_SHA256_X86_64=6f98805688d19672bd699fbbfa2c2cf0fc054ac3df1f0e6a47664d963d530255
ARG NINJA_SHA256_AARCH64=5c25c6570b0155e95fce5918cb95f1ad9870df5768653afe128db822301a05a1

RUN set -eux; \
    arch="$(uname -m)"; \
    case "$arch" in \
      x86_64)  cm_sha="$CMAKE_SHA256_X86_64";  nj_zip=ninja-linux.zip;         nj_sha="$NINJA_SHA256_X86_64" ;; \
      aarch64) cm_sha="$CMAKE_SHA256_AARCH64"; nj_zip=ninja-linux-aarch64.zip; nj_sha="$NINJA_SHA256_AARCH64" ;; \
      *) echo "arquitetura não suportada: $arch" >&2; exit 1 ;; \
    esac; \
    cd /tmp; \
    curl -fsSLo cmake.tar.gz "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-${arch}.tar.gz"; \
    echo "${cm_sha}  cmake.tar.gz" | sha256sum -c -; \
    mkdir -p /opt/cmake; \
    tar -xzf cmake.tar.gz -C /opt/cmake --strip-components=1; \
    curl -fsSLo ninja.zip "https://github.com/ninja-build/ninja/releases/download/v${NINJA_VERSION}/${nj_zip}"; \
    echo "${nj_sha}  ninja.zip" | sha256sum -c -; \
    unzip -q ninja.zip -d /usr/local/bin; \
    chmod 0755 /usr/local/bin/ninja; \
    rm -f cmake.tar.gz ninja.zip; \
    for tool in cmake ctest cpack; do ln -s "/opt/cmake/bin/$tool" "/usr/local/bin/$tool"; done; \
    cmake --version; ninja --version

# Saídas determinísticas e sem cores nos logs do avaliador.
ENV CMAKE_GENERATOR=Ninja \
    CLICOLOR_FORCE=0 \
    LC_ALL=C.UTF-8
