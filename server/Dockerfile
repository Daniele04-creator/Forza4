FROM gcc:13-bookworm

WORKDIR /app
COPY . .

RUN make
EXPOSE 5555

CMD ["./connect_four_server"]
