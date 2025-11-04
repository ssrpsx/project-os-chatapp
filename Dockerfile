FROM ubuntu:22.04

RUN apt-get update && \
    apt-get install -y g++ make build-essential && \
    apt-get clean

WORKDIR /app

COPY ./ ./app

RUN g++ -o server ./app/codes/server.cpp -pthread -lrt
RUN g++ -o server-test ./app/codes/server-no-synchronization.cpp -pthread -lrt

RUN g++ -o client ./app/codes/client.cpp -pthread -lrt
RUN g++ -o client-test ./app/codes/client-spam-chat.cpp -pthread -lrt

CMD ["/bin/bash"]
