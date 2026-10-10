# Container for the Lichess bot bridge. The engine is built at image build time
# with a plain C++17 toolchain, then the bot runs it as a child process.
#
#   docker build -t seger-bot .
#   docker run -d --restart unless-stopped \
#     -e LICHESS_TOKEN=xxxxxxxx seger-bot --accept-rated
#
# The bot only needs outbound HTTPS to lichess.org; do not publish any ports.

FROM debian:bookworm-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ make ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY src ./src
COPY tests ./tests
COPY Makefile ./
RUN make

FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends python3 ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY tools/lichess_bot.py ./tools/lichess_bot.py
COPY --from=build /src/build/seger ./build/seger
ENV LICHESS_TOKEN=""
ENTRYPOINT ["python3", "tools/lichess_bot.py"]
