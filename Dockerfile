# ==============================================================================
# Sega Tower of Power Development Container
# Targets: Genesis / Mega Drive (SGDK), Sega 32X (SH-2), Sega CD, Mega EverDrive
# ==============================================================================

FROM debian:bookworm-slim

ARG DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies (Java JRE for SGDK rescomp/xgm2tool, make, tools)
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        default-jre-headless \
        git \
        make \
        python3 \
        genisoimage \
        tar \
        gzip \
        ca-certificates && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Setup toolchain directory
ENV MARSDEV=/opt/marsdev
ENV GDK=/opt/marsdev/m68k-elf
ENV GENDEV=/opt/marsdev
ENV PATH=/opt/marsdev/bin:/opt/marsdev/m68k-elf/bin:/opt/marsdev/sh-elf/bin:${PATH}

# Unpack prebuilt toolchain artifacts from artifacts/
RUN mkdir -p /opt/marsdev
COPY artifacts/m68k-elf-toolchain.tar.gz /tmp/m68k-elf.tar.gz
COPY artifacts/sh-elf-toolchain.tar.gz /tmp/sh-elf.tar.gz
COPY artifacts/marsdev-tools.tar.gz /tmp/marsdev-tools.tar.gz

RUN tar -xzf /tmp/m68k-elf.tar.gz -C /opt/marsdev && \
    tar -xzf /tmp/sh-elf.tar.gz -C /opt/marsdev && \
    tar -xzf /tmp/marsdev-tools.tar.gz -C /opt/marsdev && \
    rm -f /tmp/*.tar.gz && \
    chmod -R ugo+rx /opt/marsdev/bin /opt/marsdev/m68k-elf/bin /opt/marsdev/sh-elf/bin

WORKDIR /work

ENTRYPOINT ["/bin/bash", "-c"]
CMD ["make"]
