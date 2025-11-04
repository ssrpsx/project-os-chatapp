FROM ubuntu:22.04

RUN apt-get update && \
    apt-get install -y g++ make build-essential && \
    apt-get clean

WORKDIR /app

# COPY ./ ./app

# RUN g++ -o server server.cpp -pthread -lrt
# RUN g++ -o client client.cpp -pthread -lrt

CMD ["/bin/bash"]
