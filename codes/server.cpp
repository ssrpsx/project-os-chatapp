#include "header.h"

std::mutex mtx;
// Data Structures สำหรับเก็บสถานะ Server
std::map<std::string, std::string> client_rooms;  // key: qname, value: room_name
std::map<std::string, std::string> client_names;  // key: qname, value: user_name
std::map<std::string, std::string> client_queues; // key: user_name, value: qname

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

    std::cout << "\033[32m" << name << " (" << qname << ") has joined the server! (Room: " << room << ")\033[0m" << std::endl;
    send_to_client(qname, "[SERVER] Welcome! You are in the lobby.");
}

// เมื่อมีคนออกจากระบบ (Quit)
void handle_quit(const std::string &msg)
{
    // msg = "QUIT:/client_Phai"
    std::string qname = msg.substr(5);
    std::lock_guard<std::mutex> lock(mtx);

    auto it_name = client_names.find(qname);
    if (it_name != client_names.end())
    {
        std::string name = it_name->second;
        std::cout << "\033[31m" << name << " (" << qname << ") has left the server.\033[0m" << std::endl;
        
        client_queues.erase(name); // ลบจาก map (name -> qname)
        client_names.erase(it_name); // ลบจาก map (qname -> name)
    }

    client_rooms.erase(qname); // ลบจาก map (qname -> room)
}

// เมื่อมีคนย้ายห้อง (Join)
void handle_join(const std::string &msg)
{
    // msg = "JOIN:/client_Phai:DevRoom"
    int pos1 = msg.find(':');
    int pos2 = msg.find(':', pos1 + 1);
    
    std::string qname = msg.substr(pos1 + 1, pos2 - (pos1 + 1));
    std::string room = msg.substr(pos2 + 1);

    std::lock_guard<std::mutex> lock(mtx);
    
    if (client_names.find(qname) != client_names.end())
    {
        client_rooms[qname] = room;
        std::cout << "\033[34m" << client_names[qname] << " moved to room: " << room << "\033[0m" << std::endl;
        send_to_client(qname, "[SERVER] You joined room: " + room);
    }
}

// เมื่อมีคนออกจากห้อง (Leave)
void handle_leave_room(const std::string &msg)
{
    // msg = "LEAVE:/client_Phai"
    std::string qname = msg.substr(6);
    std::string room = "lobby";

    std::lock_guard<std::mutex> lock(mtx);
    if (client_names.find(qname) != client_names.end())
    {
        client_rooms[qname] = room;
        std::cout << "\033[34m" << client_names[qname] << " returned to lobby.\033[0m" << std::endl;
        send_to_client(qname, "[SERVER] You returned to the lobby.");
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
    
    std::lock_guard<std::mutex> lock(mtx);

    // 1. ค้นหาข้อมูลผู้ส่ง
    auto it_sender = client_rooms.find(sender_qname);
    auto it_name = client_names.find(sender_qname);
    
    if (it_sender == client_rooms.end() || it_name == client_names.end()) {
        return; // ไม่เจอผู้ส่ง
    }
    sender_room = it_sender->second;
    sender_name = it_name->second;

    std::string final_msg = sender_name + "[" + sender_room + "]: " + text;
    std::cout << "\033[35m" << "Broadcasting in [" << sender_room << "] from " << sender_name << "\033[0m" << std::endl;

    // 2. ส่งหาทุกคนในห้อง (รวมถึงตัวเอง)
    for (const auto& pair : client_rooms)
    {
        const std::string& receiver_qname = pair.first;
        const std::string& receiver_room = pair.second;

        if (receiver_room == sender_room)
        {
            send_to_client(receiver_qname, final_msg);
        }
    }
}

// เมื่อมีคนส่ง DM
void handle_dm(const std::string &msg)
{
    // msg = "DM:/client_Phai:Gemini:Hi there"
    int pos1 = msg.find(':'); // 2
    int pos2 = msg.find(':', pos1 + 1); // 15 (สมมติ)
    int pos3 = msg.find(':', pos2 + 1); // 22 (สมมติ)

    std::string sender_qname = msg.substr(pos1 + 1, pos2 - (pos1 + 1));
    std::string target_name = msg.substr(pos2 + 1, pos3 - (pos2 + 1));
    std::string text = msg.substr(pos3 + 1);

    std::lock_guard<std::mutex> lock(mtx);

    std::string sender_name = client_names[sender_qname];
    std::string target_qname = client_queues[target_name];

    if (target_qname.empty())
    {
        send_to_client(sender_qname, "[SERVER] User '" + target_name + "' not found.");
        return;
    }

    // ส่ง DM ไปหาเป้าหมาย
    std::string dm_msg = "[DM from " + sender_name + "]: " + text;
    send_to_client(target_qname, dm_msg);

    // ส่งข้อความยืนยันกลับไปหาผู้ส่ง
    std::string confirm_msg = "[DM to " + target_name + "]: " + text;
    send_to_client(sender_qname, confirm_msg);
}

// เมื่อมีคนขอรายชื่อ (Who)
void handle_who(const std::string &msg)
{
    // msg = "WHO:/client_Phai"
    std::string qname = msg.substr(4);
    
    std::lock_guard<std::mutex> lock(mtx);

    std::string room = client_rooms[qname];
    std::string user_list = "[SERVER] Users in [" + room + "]:\n";
    
    for (const auto& pair : client_rooms)
    {
        // pair.first คือ qname, pair.second คือ room_name
        if (pair.second == room)
        {
            user_list += "  - " + client_names[pair.first] + "\n";
        }
    }
    
    send_to_client(qname, user_list);
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
            } else if (msg.find("DM:") == 0) { // เพิ่ม
                std::thread(handle_dm, msg).detach();
            } else if (msg.find("WHO:") == 0) { // เพิ่ม
                std::thread(handle_who, msg).detach();
            } else if (msg.find("LEAVE:") == 0) { // เพิ่ม
                std::thread(handle_leave_room, msg).detach();
            } else if (msg.find("QUIT:") == 0) { // เปลี่ยนจาก LEAVE
                std::thread(handle_quit, msg).detach();
            }
        }
    }
    mq_close(server_q);
    mq_unlink(CONTROL_Q);
    return 0;
}