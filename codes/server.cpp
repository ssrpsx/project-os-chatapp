    #include "header.h"

    std::mutex mtx;
    std::map<std::string, std::string> client_rooms;  // qname -> room_name
    std::map<std::string, std::string> client_names;  // qname -> user_name
    std::map<std::string, std::string> client_queues; // user_name -> qname

    // เพิ่ม: สำหรับ Heartbeat
    std::map<std::string, std::chrono::steady_clock::time_point> last_heartbeat;

    // ฟังก์ชันช่วย: ส่งข้อความหา client โดยใช้ qname
    void send_to_client(const std::string& qname, const std::string& msg)
    {
        mqd_t client_q = mq_open(qname.c_str(), O_WRONLY);
        if (client_q != (mqd_t)-1)
        {
            mq_send(client_q, msg.c_str(), msg.size() + 1, 0);
            mq_close(client_q);
        }
    }

    // ฟังก์ชันช่วย: ส่งข้อความหาทุกคนในห้อง (ข้าม qname_to_skip ได้)
    void broadcast_to_room(const std::string& room, const std::string& msg, const std::string& qname_to_skip)
    {
        // ฟังก์ชันนี้ต้องถูกเรียกหลังจากปลดล็อค mtx หลัก
        // เพื่อป้องกันการ deadlock ถ้ามันไปเรียกใช้ handler อื่น
        std::vector<std::string> qnames_in_room;

        mtx.lock();
        for (const auto& pair : client_rooms) {
            if (pair.second == room && pair.first != qname_to_skip) {
                qnames_in_room.push_back(pair.first);
            }
        }
        mtx.unlock();

        for (const std::string& qname : qnames_in_room) {
            send_to_client(qname, msg);
        }
    }


    // เมื่อมีคนเชื่อมต่อ
    void handle_register(const std::string &msg)
    {
        // msg = "REGISTER:/client_Phai:Phai"
        int pos1 = msg.find(':');
        int pos2 = msg.find(':', pos1 + 1);
        
        std::string qname = msg.substr(pos1 + 1, pos2 - (pos1 + 1));
        std::string name = msg.substr(pos2 + 1);
        std::string room = "lobby";

        std::lock_guard<std::mutex> lock(mtx);
        client_rooms[qname] = room;
        client_names[qname] = name;
        client_queues[name] = qname;
        last_heartbeat[qname] = std::chrono::steady_clock::now(); // เพิ่ม: ลงทะเบียน Heartbeat

        std::cout << "\033[32m" << name << " (" << qname << ") has joined the server! (Room: " << room << ")\033[0m" << std::endl;
        send_to_client(qname, "[SERVER] Welcome! You are in the lobby.");
    }

    // เมื่อมีคนออกจากระบบ (Quit)
    void handle_quit(const std::string &msg)
    {
        // msg = "QUIT:/client_Phai"
        std::string qname = msg.substr(5);
        std::string name;
        std::string room;

        mtx.lock();
        if (client_names.find(qname) == client_names.end()) {
            mtx.unlock(); return; // ไม่เจอ หรือออกไปแล้ว
        }
        
        name = client_names[qname];
        room = client_rooms[qname];

        client_queues.erase(name);
        client_names.erase(qname);
        client_rooms.erase(qname);
        last_heartbeat.erase(qname); // ลบจาก Heartbeat
        
        mtx.unlock(); // ปลดล็อคก่อนค่อย broadcast

        std::cout << "\033[31m" << name << " (" << qname << ") has left the server.\033[0m" << std::endl;
        
        // แจ้งเตือนคนที่เหลือในห้อง
        std::string leave_msg = "[SERVER] " + name + " has left.";
        broadcast_to_room(room, leave_msg, ""); // ส่งหาทุกคน
    }

    // เมื่อมีคนย้ายห้อง (Join)
    void handle_join(const std::string &msg)
    {
        // msg = "JOIN:/client_Phai:DevRoom"
        int pos1 = msg.find(':');
        int pos2 = msg.find(':', pos1 + 1);
        
        std::string qname = msg.substr(pos1 + 1, pos2 - (pos1 + 1));
        std::string room = msg.substr(pos2 + 1);
        std::string name;

        mtx.lock();
        if (client_names.find(qname) != client_names.end())
        {
            client_rooms[qname] = room;
            name = client_names[qname];
            std::cout << "\033[34m" << name << " moved to room: " << room << "\033[0m" << std::endl;
        }
        mtx.unlock(); // ปลดล็อคก่อน

        if (!name.empty()) {
            send_to_client(qname, "[SERVER] You joined room: " + room);
            
            // เพิ่ม: แจ้งเตือนคนอื่นในห้อง
            std::string join_msg = "[SERVER] " + name + " has joined the room.";
            broadcast_to_room(room, join_msg, qname); // ส่งหาทุกคนในห้อง ยกเว้นตัวเอง
        }
    }

    // เมื่อมีคนออกจากห้อง (Leave)
    void handle_leave_room(const std::string &msg)
    {
        // msg = "LEAVE:/client_Phai"
        std::string qname = msg.substr(6);
        std::string room = "lobby";
        std::string old_room;
        std::string name;

        mtx.lock();
        if (client_names.find(qname) != client_names.end())
        {
            old_room = client_rooms[qname];
            name = client_names[qname];
            client_rooms[qname] = room; // ย้ายกลับ lobby
            std::cout << "\033[34m" << name << " returned to lobby.\033[0m" << std::endl;
        }
        mtx.unlock();

        if (!name.empty()) {
            send_to_client(qname, "[SERVER] You returned to the lobby.");
            // แจ้งเตือนคนในห้องเก่า
            std::string leave_msg = "[SERVER] " + name + " has left the room.";
            broadcast_to_room(old_room, leave_msg, qname);
        }
    }

    // เมื่อมีคนส่งข้อความ (Say)
    void broadcast_room(const std::string &msg)
    {
        // msg = "SAY:/client_Phai:Hello everyone"
        int pos1 = msg.find(':');
        int pos2 = msg.find(':', pos1 + 1);

        std::string sender_qname = msg.substr(pos1 + 1, pos2 - (pos1 + 1));
        std::string text = msg.substr(pos2 + 1);

        std::string sender_room;
        std::string sender_name;
        
        mtx.lock();
        auto it_sender = client_rooms.find(sender_qname);
        auto it_name = client_names.find(sender_qname);
        
        if (it_sender == client_rooms.end() || it_name == client_names.end()) {
            mtx.unlock(); return;
        }
        sender_room = it_sender->second;
        sender_name = it_name->second;
        mtx.unlock(); // ปลดล็อคตรงนี้

        // 1. สร้างข้อความปกติ
        std::string final_msg = sender_name + "[" + sender_room + "]: " + text;
        
        // 2. ประทับเวลาของ Server (ใช้ system_clock)
        long long timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch()
                                ).count();

        // 3. สร้าง Payload ที่จะส่ง = "ข้อความ:เวลา"
        std::string payload = final_msg + ":" + std::to_string(timestamp_ms);

        // ส่งหาทุกคนในห้อง (รวมถึงตัวเอง)
        // broadcast_to_room จะไป lock mtx ของมันเอง
        broadcast_to_room(sender_room, payload, "");
    }

    // เมื่อมีคนส่ง DM
    void handle_dm(const std::string &msg)
    {
        // msg = "DM:/client_Phai:Gemini:Hi there"
        int pos1 = msg.find(':');
        int pos2 = msg.find(':', pos1 + 1);
        int pos3 = msg.find(':', pos2 + 1);

        std::string sender_qname = msg.substr(pos1 + 1, pos2 - (pos1 + 1));
        std::string target_name = msg.substr(pos2 + 1, pos3 - (pos2 + 1));
        std::string text = msg.substr(pos3 + 1);

        std::string sender_name;
        std::string target_qname;

        mtx.lock();
        sender_name = client_names[sender_qname];
        
        if (sender_name == target_name) {
            mtx.unlock();
            send_to_client(sender_qname, "[SERVER] You cannot send a DM to yourself.");
            return;
        }
        
        target_qname = client_queues[target_name];
        mtx.unlock();

        if (target_qname.empty())
        {
            send_to_client(sender_qname, "[SERVER] User '" + target_name + "' not found.");
            return;
        }

        // 1. ประทับเวลาของ Server (ใช้ system_clock)
        long long timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch()
                                ).count();
        std::string ts_str = ":" + std::to_string(timestamp_ms);

        // ส่ง DM ไปหาเป้าหมาย (พร้อม timestamp)
        std::string dm_msg = "[DM from " + sender_name + "]: " + text + ts_str;
        send_to_client(target_qname, dm_msg);

        // ส่งข้อความยืนยันกลับไปหาผู้ส่ง (พร้อม timestamp)
        std::string confirm_msg = "[DM to " + target_name + "]: " + text + ts_str;
        send_to_client(sender_qname, confirm_msg);
    }

    // เมื่อมีคนขอรายชื่อ (Who)
    void handle_who(const std::string &msg)
    {
        // msg = "WHO:/client_Phai"
        std::string qname = msg.substr(4);
        std::string room;
        std::string user_list = "";
        
        mtx.lock();
        room = client_rooms[qname];
        user_list = "[SERVER] Users in [" + room + "]:\n";
        
        for (const auto& pair : client_rooms)
        {
            if (pair.second == room)
            {
                user_list += "  - " + client_names[pair.first] + "\n";
            }
        }
        mtx.unlock();
        
        send_to_client(qname, user_list);
    }

    void handle_heartbeat(const std::string &msg)
    {
        // msg = "BEAT:/client_Phai"
        std::string qname = msg.substr(5);
        std::lock_guard<std::mutex> lock(mtx);
        // อัปเดตเวลาล่าสุดที่ client นี้ยังอยู่
        if (last_heartbeat.find(qname) != last_heartbeat.end()) {
            last_heartbeat[qname] = std::chrono::steady_clock::now();
        }
    }

    void reaper_thread()
    {
        while(true)
        {
            // ตรวจสอบทุก 10 วินาที
            std::this_thread::sleep_for(std::chrono::seconds(10));
            
            std::vector<std::string> qnames_to_kick;
            
            mtx.lock();
            auto now = std::chrono::steady_clock::now();
            for (const auto& pair : last_heartbeat)
            {
                // ถ้าไม่เจอ qname ใน client_names (อาจจะเพิ่งออกไป) ก็ข้าม
                if (client_names.find(pair.first) == client_names.end()) continue;

                auto diff_sec = std::chrono::duration_cast<std::chrono::seconds>(now - pair.second).count();
                
                // ถ้า client ขาดการติดต่อเกิน 20 วินาที
                if (diff_sec > 20)
                {
                    qnames_to_kick.push_back(pair.first);
                }
            }
            mtx.unlock();
            
            // ไล่เตะ client ที่หมดเวลา
            for (const std::string& qname : qnames_to_kick)
            {
                std::string name_to_kick;
                mtx.lock();
                name_to_kick = client_names[qname];
                mtx.unlock();

                std::cout << "\033[31mReaper: " << name_to_kick << " (" << qname << ") timed out. Kicking.\033[0m" << std::endl;
                
                std::thread(handle_quit, "QUIT:" + qname).detach();
            }
        }
    }

    // Main loop ของ Server
    int main()
    {
        mq_unlink(CONTROL_Q);
        struct mq_attr attr;
        attr.mq_flags = 0;
        attr.mq_maxmsg = 10;
        attr.mq_msgsize = size_of_message;
        attr.mq_curmsgs = 0;

        mqd_t server_q = mq_open(CONTROL_Q, O_CREAT | O_RDWR, 0666, &attr);
        if (server_q == (mqd_t)-1) {
            perror("mq_open server"); return 1;
        }
        std::cout << "\033[32m" << "Server opened" << "\033[0m" << std::endl;

        std::thread(reaper_thread).detach();
        std::cout << "\033[33mReaper thread started (Timeout: 20s)\033[0m" << std::endl;

        char buffer[size_of_message];
        while (true)
        {
            ssize_t bytes = mq_receive(server_q, buffer, sizeof(buffer), nullptr);
            if (bytes > 0)
            {
                buffer[bytes] = '\0';
                std::string msg(buffer);

                if (msg.find("REGISTER:") == 0) {
                    std::thread(handle_register, msg).detach();
                } else if (msg.find("JOIN:") == 0) {
                    std::thread(handle_join, msg).detach();
                } else if (msg.find("SAY:") == 0) {
                    std::thread(broadcast_room, msg).detach();
                } else if (msg.find("DM:") == 0) {
                    std::thread(handle_dm, msg).detach();
                } else if (msg.find("WHO:") == 0) {
                    std::thread(handle_who, msg).detach();
                } else if (msg.find("LEAVE:") == 0) {
                    std::thread(handle_leave_room, msg).detach();
                } else if (msg.find("QUIT:") == 0) {
                    std::thread(handle_quit, msg).detach();
                } else if (msg.find("BEAT:") == 0) {
                    std::thread(handle_heartbeat, msg).detach();
                }
            }
        }
        mq_close(server_q);
        mq_unlink(CONTROL_Q);
        return 0;
    }