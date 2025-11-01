#include "header.h"

// ฟังก์ชันนี้จะคอยฟัง message queue ของตัวเอง
void listen_queue(const std::string &qname)
{
    mqd_t client_q = mq_open(qname.c_str(), O_RDONLY);
    if (client_q == (mqd_t)-1) {
        perror("mq_open client listen");
        return;
    }

    char buffer[size_of_message];
    while (true)
    {
        ssize_t bytes = mq_receive(client_q, buffer, sizeof(buffer), nullptr);
        if (bytes <= 0) {
            break; // Queue ถูกลบ หรือเกิด Error
        }

        buffer[bytes] = '\0';
        std::string msg(buffer);

        // จัดการแสดงผลและสี
        if (msg.find("[DM from") == 0 || msg.find("[DM to") == 0) {
            // Magenta (สีม่วง) สำหรับ DMs
            std::cout << "\033[35m" << msg << "\033[0m" << std::endl << "> " << std::flush;
        } else if (msg.find("[SERVER]") == 0) {
            // Yellow (สีเหลือง) สำหรับข้อความ Server
            std::cout << "\033[33m" << msg << "\033[0m" << std::endl << "> " << std::flush;
        } else {
            // Green (สีเขียว) สำหรับแชทในห้อง
            std::cout << "\033[32m" << msg << "\033[0m" << std::endl << "> " << std::flush;
        }
    }
    mq_close(client_q);
}

// ฟังก์ชันแสดงคำสั่ง
void show_help()
{
    std::cout << "--- Help ---\n";
    std::cout << "SAY: <message>   (Send message to current room)\n";
    std::cout << "JOIN: <room_name> (Join a new room)\n";
    std::cout << "DM: <user> <msg>   (Send Direct Message)\n";
    std::cout << "WHO                (List users in current room)\n";
    std::cout << "LEAVE              (Leave current room, return to lobby)\n";
    std::cout << "QUIT               (Disconnect from the server)\n";
    std::cout << "HELP               (Show this help message)\n";
    std::cout << "------------" << std::endl;
}

int main()
{   
    std::string client_name;
    std::string client_qname;

    std::cout << "\nEnter your ChatName: ";
    std::getline(std::cin, client_name);
    client_qname = "/client_" + client_name;

    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = size_of_message;
    attr.mq_curmsgs = 0;

    mq_unlink(client_qname.c_str()); // ลบ queue เก่า (ถ้ามี)
    mqd_t client_q = mq_open(client_qname.c_str(), O_CREAT | O_RDONLY, 0666, &attr);
    if (client_q == (mqd_t)-1) {
        perror("mq_open client"); return 1;
    }
    mq_close(client_q);
    
    std::thread t(listen_queue, client_qname);
    
    mqd_t server_q = mq_open(CONTROL_Q, O_WRONLY);
    if (server_q == (mqd_t)-1) {
        perror("mq_open server_q");
        t.join();
        mq_unlink(client_qname.c_str());
        return 1;
    }

    // ส่ง Register message (เพิ่ม client_name เข้าไปด้วย)
    std::string reg_msg = "REGISTER:" + client_qname + ":" + client_name;
    mq_send(server_q, reg_msg.c_str(), reg_msg.size() + 1, 0);

    std::cout << "\nRegistered as " << client_name << std::endl;
    show_help();
    std::cout << "> " << std::flush;
    
    std::string msg;
    while (std::getline(std::cin, msg))
    {
        if (msg.empty()) {
            std::cout << "> " << std::flush; continue;
        }

        if (msg.find("SAY:") == 0) {
            // "SAY:/client_Phai:Hello"
            std::string send_msg = "SAY:" + client_qname + ":" + msg.substr(4);
            mq_send(server_q, send_msg.c_str(), send_msg.size() + 1, 0);
        }
        else if (msg.find("JOIN:") == 0) {
            // "JOIN:/client_Phai:DevRoom"
            std::string send_msg = "JOIN:" + client_qname + ":" + msg.substr(5);
            mq_send(server_q, send_msg.c_str(), send_msg.size() + 1, 0);
        }
        else if (msg.find("DM:") == 0) {
            // "DM:Gemini Hello there"
            // ต้องหา " " แรกหลัง "DM:"
            int first_space = msg.find(' ', 4);
            if (first_space == std::string::npos) {
                std::cout << "Invalid DM format. Use: DM: <user> <message>" << std::endl;
            } else {
                std::string user = msg.substr(4, first_space - 4);
                std::string dm_text = msg.substr(first_space + 1);
                // "DM:/client_Phai:Gemini:Hello there"
                std::string send_msg = "DM:" + client_qname + ":" + user + ":" + dm_text;
                mq_send(server_q, send_msg.c_str(), send_msg.size() + 1, 0);
            }
        }
        else if (msg == "WHO") {
            // "WHO:/client_Phai"
            std::string send_msg = "WHO:" + client_qname;
            mq_send(server_q, send_msg.c_str(), send_msg.size() + 1, 0);
        }
        else if (msg == "LEAVE") {
            // "LEAVE:/client_Phai"
            std::string send_msg = "LEAVE:" + client_qname;
            mq_send(server_q, send_msg.c_str(), send_msg.size() + 1, 0);
        }
        else if (msg == "QUIT") {
            // "QUIT:/client_Phai"
            std::string send_msg = "QUIT:" + client_qname;
            mq_send(server_q, send_msg.c_str(), send_msg.size() + 1, 0);
            break; // ออกจาก loop
        }
        else if (msg == "HELP") {
            show_help();
        }
        else {
            std::cout << "Unknown command. Type 'HELP' for commands." << std::endl;
        }
        
        std::cout << "> " << std::flush;
    }
    
    mq_close(server_q);
    mq_unlink(client_qname.c_str()); // ลบ queue ของตัวเอง
    t.join(); // รอ thread ปิด
    
    std::cout << "Disconnected." << std::endl;
    return 0;
}