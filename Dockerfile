FROM docker.io/rust:1-slim-bookworm AS builder
WORKDIR /op1
RUN apt-get update && apt-get upgrade --yes && apt-get install --yes libclang-dev
COPY Cargo.toml Cargo.lock ./
COPY mbeval-sys mbeval-sys
COPY op1 op1
RUN cargo build --release

FROM docker.io/debian:bookworm-slim
RUN apt-get update && apt-get upgrade --yes
COPY --from=builder /op1/target/release/op1-server /usr/local/bin/op1-server
ENTRYPOINT ["/usr/local/bin/op1-server"]
