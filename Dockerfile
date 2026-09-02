# SPDX-License-Identifier: Unlicense
FROM ubuntu@sha256:33ceb71981b602c1a7443a53469e4dba065f7503eab3078a2d7a57a2ab987517

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        python3 \
        sdcc=4.2.0+dfsg-1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
CMD ["make", "check"]
