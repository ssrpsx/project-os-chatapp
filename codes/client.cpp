#include "header.h"

// ตัวแปร Global (หรือ static) สำหรับจับเวลา Ping
std::atomic<std::chrono::system_clock::time_point> last_command_time;
std::string client_name_global;

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
            std::cout << "\n[SERVER] Disconnected (Queue closed)." << std::endl;
            break;
        }

        buffer[bytes] = '\0';
        std::string msg(buffer);
        auto end_time = std::chrono::system_clock::now(); // เวลาที่ได้รับ

        // 1. ตรวจสอบข้อความ Server (ไม่มี timestamp)
        if (msg.find("[SERVER]") == 0) {
            std::cout << "\033[33m" << msg << "\033[0m" << std::endl << "> " << std::flush;
        }
        // 2. ถ้าไม่ใช่ Server, สันนิษฐานว่ามี timestamp (SAY หรือ DM)
        else
        {
            size_t last_colon = msg.rfind(':');
            if (last_colon == std::string::npos || last_colon == msg.length() - 1) {
                 // Fallback: แสดงผลปกติถ้าหา timestamp ไม่เจอ
                 std::cout << "\033[37m" << msg << "\033[0m" << std::endl << "> " << std::flush;
                 continue;
            }

            // แยกข้อความและ timestamp
            std::string msg_text = msg.substr(0, last_colon); // "Phai[lobby]:Hi" หรือ "[DM to...]:Hi"
            std::string msg_timestamp_str = msg.substr(last_colon + 1); // "123456789"

            std::string latency_str = "";
            std::string color_code = "\033[32m"; // สีเขียว (SAY)

            // ตรวจสอบว่าเป็น DM หรือไม่
            bool is_dm_to = (msg_text.find("[DM to") == 0);
            bool is_dm_from = (msg_text.find("[DM from") == 0);
            bool is_dm = is_dm_to || is_dm_from;

            // หาชื่อผู้ส่ง (สำหรับ SAY)
            int pos1 = msg_text.find('[');
            std::string sender_in_msg = (!is_dm && pos1 != std::string::npos) ? msg_text.substr(0, pos1) : "";

            if (is_dm) {
                color_code = "\033[35m"; // สีม่วง (DM)
            }

            try {
                // เคส 1: ข้อความ RTT (ข้อความ SAY ของเรา หรือ DM ยืนยันของเรา)
                if ((!is_dm && sender_in_msg == client_name_global) || (is_dm_to))
                {
                    auto rtt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    end_time - last_command_time.load() // ใช้ last_command_time
                                  ).count();
                    latency_str = " \033[34m(" + std::to_string(rtt_ms) + "ms)\033[0m"; // RTT สีน้ำเงิน
                }
                // เคส 2: ข้อความ S2C (ข้อความ SAY คนอื่น หรือ DM จากคนอื่น)
                else
                {
                    long long server_timestamp_ms = std::stoll(msg_timestamp_str);
                    auto end_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                            end_time.time_since_epoch()
                                       ).count();
                    
                    long long s2c_ms = end_time_ms - server_timestamp_ms;
                    if (s2c_ms < 0) s2c_ms = 0; // กันนาฬิกาเพี้ยน
                    
                    latency_str = " \033[36m(" + std::to_string(s2c_ms) + "ms)\033[0m"; // S2C สีฟ้าอ่อน
                }
            } catch (...) {
                latency_str = " (err)";
            }

            // แสดงผล
            std::cout << color_code << msg_text << latency_str << "\033[0m" << std::endl << "> " << std::flush;
        }
    }
    mq_close(client_q);
}

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

// เพิ่ม: Thread สำหรับส่ง Heartbeat
void heartbeat_thread(std::string qname)
{
    std::string beat_msg = "BEAT:" + qname;
    while(true)
    {
        // ส่งสัญญาณชีพทุก 10 วินาที
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        mqd_t hb_server_q = mq_open(CONTROL_Q, O_WRONLY);
        if (hb_server_q == (mqd_t)-1) {
            // Server อาจจะปิดไปแล้ว
            continue; // ลองใหม่รอบหน้า
        }
        
        if (mq_send(hb_server_q, beat_msg.c_str(), beat_msg.size() + 1, 0) == -1) {
             // Error, อาจจะ Server ปิด
        }
        mq_close(hb_server_q);
    }
}


int main()
{   
    std::cout << "\nEnter your ChatName: ";
    std::getline(std::cin, client_name_global); // ใช้ตัวแปร global
    if (client_name_global.empty()) {
        std::cout << "Name cannot be empty." << std::endl; return 1;
    }
    std::string client_qname = "/client_" + client_name_global;

    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = size_of_message;
    attr.mq_curmsgs = 0;

    mq_unlink(client_qname.c_str());
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

    // ส่ง Register message
    std::string reg_msg = "REGISTER:" + client_qname + ":" + client_name_global;
    mq_send(server_q, reg_msg.c_str(), reg_msg.size() + 1, 0);
    std::thread(heartbeat_thread, client_qname).detach();

    // เพิ่ม: สตาร์ท Heartbeat Thread
    std::thread(heartbeat_thread, client_qname).detach();


    std::cout << "\nRegistered as " << client_name_global << std::endl;
    std::cout << "Type 'HELP' for commands." << std::endl;
    std::cout << "> " << std::flush;
    
    std::string msg;
    while (std::getline(std::cin, msg))
    {
        if (msg.empty()) {
            std::cout << "> " << std::flush; continue;
        }

        if (msg.find("SAY:") == 0) {
            std::string text = msg.substr(4);
            if (text.empty()) continue;
            
            last_command_time = std::chrono::system_clock::now(); // เริ่มจับเวลา
            
            std::string send_msg = "SAY:" + client_qname + ":" + text;
            mq_send(server_q, send_msg.c_str(), send_msg.size() + 1, 0);
        }
        else if (msg.find("JOIN:") == 0) {
            // (JOIN ไม่ต้องจับเวลา)
            std::string room = msg.substr(5);
            if (room.empty()) continue;
            std::string send_msg = "JOIN:" + client_qname + ":" + room;
            mq_send(server_q, send_msg.c_str(), send_msg.size() + 1, 0);
        }
        else if (msg.find("DM:") == 0) {
            int first_space = msg.find(' ', 4);
            if (first_space == std::string::npos || first_space + 1 == msg.length()) {
                std::cout << "Invalid DM format. Use: DM: <user> <message>" << std::endl;
            } else {
                std::string user = msg.substr(4, first_space - 4);
                std::string dm_text = msg.substr(first_space + 1);
                
                last_command_time = std::chrono::system_clock::now(); // เริ่มจับเวลา
                
                std::string send_msg = "DM:" + client_qname + ":" + user + ":" + dm_text;
                mq_send(server_q, send_msg.c_str(), send_msg.size() + 1, 0);
            }
        }
        else if (msg == "WHO") {
            std::string send_msg = "WHO:" + client_qname;
            mq_send(server_q, send_msg.c_str(), send_msg.size() + 1, 0);
        }
        else if (msg == "LEAVE") {
            std::string send_msg = "LEAVE:" + client_qname;
            mq_send(server_q, send_msg.c_str(), send_msg.size() + 1, 0);
        }
        else if (msg == "QUIT") {
            std::string send_msg = "QUIT:" + client_qname;
            mq_send(server_q, send_msg.c_str(), send_msg.size() + 1, 0);
            break;
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
    mq_unlink(client_qname.c_str());
    t.join(); 
    
    std::cout << "Disconnected." << std::endl;
    return 0;
}