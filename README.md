# 💬 OS Message Queue Chat Application

A multi-client text-based chat system written in C++, using POSIX Message Queues (mqueue) for inter-process communication (IPC) between multiple clients and a central server — all at the Operating System level.

## 💡 Project Idea
This project simulates a basic real-time chat system that communicates via POSIX Message Queues instead of traditional network sockets (TCP/IP).
It demonstrates how processes can interact and synchronize efficiently through OS-level mechanisms.

Each user (client) can:

- Join a chat room
- Send public messages (SAY)
- Send private messages (DM)
- View users in the same room (WHO)
- Leave a room (LEAVE)
- Disconnect from the system (QUIT)

Additional features include:

- ✅ Latency & RTT (Round Trip Time) measurement
- ❤️ Heartbeat system to monitor connection health
- ⚙️ Multithreading for concurrent command handling

## 🧩 System Structure
### 🖥️ Server

- Acts as the central hub for all message queues.
- Listens to a global queue /control_q to receive messages from clients.
- Uses multithreading to handle each type of command concurrently (e.g., REGISTER, SAY, DM, WHO, etc.).
- Includes a Heartbeat + Reaper Thread mechanism that automatically disconnects inactive clients (timeout > 20 seconds).

### 👤 Client
- Each client creates its own unique message queue (e.g., /client_Phai).
- Sends commands and receives responses via its dedicated queue.
- Displays message latency (RTT/S2C) using colored output for clarity.

## 🎨 Color-Meaning
🟢 Green-Public message (SAY) <br>
🟣 Purple-Private message (DM) <br>
🔵 Blue-RTT of own message <br>
💠 Light Blue-Server-to-Client latency (S2C)

---

📦 project/ <br>
 ┣ 📜 Dockerfile <br>
 ┣ 📜 Docker-compose <br>
 ┣ 📂 codes/ <br>
 ┃  ┣ 📜 client.cpp <br>
 ┃  ┣ 📜 client-spam-chat.cpp <br>
 ┃  ┣ 📜 server.cpp <br>
 ┃  ┣ 📜 server-no-synchronization.cpp <br>
 ┃  ┗ 📜 header.h <br>
 ┗ 📜 README.md <br>

---

## 🧠 Core Functionality Overview
### 🔹 Server.cpp

- Opens the main message queue /control_q.

- Receives and categorizes client messages into various command types:
REGISTER, SAY, DM, JOIN, LEAVE, WHO, and BEAT.

- Spawns a new thread to handle each request asynchronously.

- Uses std::mutex to prevent race conditions during shared data access.

- Runs a background reaper_thread() that periodically checks for inactive clients (those who stopped sending heartbeat signals for over 20 seconds) and removes them automatically.

### 🔹 Client.cpp

- Registers itself with the server (REGISTER).

- Continuously listens for incoming messages through its personal queue (listen_queue).

- Sends chat commands to the server (SAY, JOIN, DM, WHO, LEAVE, QUIT).

- Maintains a heartbeat thread that sends a signal every 5 seconds to keep the connection alive.

- Measures and displays latency (RTT and S2C delay) with different color codes for better visualization.