#include "header.h"

std::atomic<long long> last_command_time_ms{0};
std::string client_name_global;
std::atomic<bool> running(true);

// ฟังก์ชันนี้จะคอยฟัง message queue ของตัวเอง (ใช้ mq_timedreceive เพื่อให้หลุดเมื่อ running==false)
void listen_queue(const std::string &qname)
{
    mqd_t client_q = mq_open(qname.c_str(), O_RDONLY);
    if (client_q == (mqd_t)-1) {
        perror("mq_open client listen");
        return;
    }

    char buffer[size_of_message];
    while (running)
    {
        // ตั้ง timeout แบบ relative ~1 วินาที
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        ssize_t bytes = mq_timedreceive(client_q, buffer, sizeof(buffer), nullptr, &ts);
        if (bytes == -1) {
            if (errno == ETIMEDOUT) {
                // timeout — ตรวจสอบ running แล้ววนใหม่
                continue;
            } else {
                // อื่น ๆ เช่น queue ถูกปิด
                std::cout << "\n[SERVER] Disconnected (mq_timedreceive error): " << strerror(errno) << std::endl;
                break;
            }
        }

        if (bytes <= 0) {
            std::cout << "\n[SERVER] Disconnected (Queue closed)." << std::endl;
            break;
        }

        // แน่ใจว่า null-terminated
        if (bytes >= (ssize_t)sizeof(buffer)) bytes = sizeof(buffer) - 1;
        buffer[bytes] = '\0';
        std::string msg(buffer);

        long long end_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch()
                                ).count();
        
        // 1. ข้อความจาก Server (ไม่มี timestamp)
        if (msg.find("[SERVER]") == 0) {
            std::cout << "\033[33m" << msg << "\033[0m" << std::endl << "> " << std::flush;
        }
        else
        {
            size_t last_colon = msg.rfind(':');
            if (last_colon == std::string::npos || last_colon == msg.length() - 1) {
                 std::cout << "\033[37m" << msg << "\033[0m" << std::endl << "> " << std::flush;
                 continue;
            }

            std::string msg_text = msg.substr(0, last_colon);
            std::string msg_timestamp_str = msg.substr(last_colon + 1);
            std::string latency_str = "";
            std::string color_code = "\033[32m";

            bool is_dm_to = (msg_text.find("[DM to") == 0);
            bool is_dm_from = (msg_text.find("[DM from") == 0);
            bool is_dm = is_dm_to || is_dm_from;
            size_t pos1 = msg_text.find('[');
            std::string sender_in_msg = (!is_dm && pos1 != std::string::npos) ? msg_text.substr(0, pos1) : "";

            if (is_dm) color_code = "\033[35m";

            try {
                if ((!is_dm && sender_in_msg == client_name_global) || (is_dm_to))
                {
                    long long rtt_ms = 0;
                    long long last = last_command_time_ms.load();
                    if (last > 0) rtt_ms = end_time_ms - last;
                    if (rtt_ms < 0) rtt_ms = 0;
                    latency_str = " \033[34m(" + std::to_string(rtt_ms) + "ms)\033[0m";
                }
                else
                {
                    long long server_timestamp_ms = std::stoll(msg_timestamp_str);
                    long long s2c_ms = end_time_ms - server_timestamp_ms;
                    if (s2c_ms < 0) s2c_ms = 0;
                    latency_str = " \033[36m(" + std::to_string(s2c_ms) + "ms)\033[0m";
                }
            } catch (...) {
                latency_str = " (err)";
            }

            std::cout << color_code << msg_text << latency_str << "\033[0m" << std::endl << "> " << std::flush;
        }
    }
    
    mq_close(client_q);
}

#include <random>
#include <thread>
#include <vector>

// aggressive spammer: หลาย worker thread ส่งคำสั่งหลายแบบเพื่อ force races
void aggressive_worker(mqd_t server_q, const std::string &client_qname, int id, int actions_per_worker) {
    std::mt19937 rng((unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count() + id);
    std::uniform_int_distribution<int> action_dist(0, 5); // 0=SAY,1=JOIN,2=LEAVE,3=WHO,4=DM,5=QUIT+REGISTER
    std::uniform_int_distribution<int> sleep_dist(5, 20);
    std::uniform_int_distribution<int> room_dist(1, 5); // room id
    std::uniform_int_distribution<int> name_dist(1, 10); // fake target id for DM

    for (int i = 0; running && i < actions_per_worker; ++i) {
        int action = action_dist(rng);
        std::string msg;
        long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch()
                            ).count();
        // update last_command_time for RTT measurement (when relevant)
        last_command_time_ms.store(now_ms);

        switch (action) {
            case 0: { // SAY
                std::string text = "spam SAY from " + client_name_global + "-" + std::to_string(id) + "-" + std::to_string(i);
                msg = "SAY:" + client_qname + ":" + text;
                break;
            }
            case 1: { // JOIN random room
                std::string room = "room" + std::to_string(room_dist(rng));
                msg = "JOIN:" + client_qname + ":" + room;
                break;
            }
            case 2: { // LEAVE
                msg = "LEAVE:" + client_qname;
                break;
            }
            case 3: { // WHO
                msg = "WHO:" + client_qname;
                break;
            }
            case 4: { // DM to some other fake name (might not exist)
                std::string target = "client_" + std::to_string(name_dist(rng));
                std::string text = "hello DM from " + client_name_global;
                msg = "DM:" + client_qname + ":" + target + ":" + text;
                break;
            }
            case 5: { // QUIT then immediate REGISTER again (forces removal & re-add)
                // QUIT
                msg = "QUIT:" + client_qname;
                if (mq_send(server_q, msg.c_str(), msg.size() + 1, 0) == -1) {
                    perror("mq_send QUIT");
                }
                // small pause to let server handle quit
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                // REGISTER again
                std::string reg_msg = "REGISTER:" + client_qname + ":" + client_name_global;
                if (mq_send(server_q, reg_msg.c_str(), reg_msg.size() + 1, 0) == -1) {
                    perror("mq_send REGISTER");
                }
                // continue to next loop iteration (we already sent)
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_dist(rng)));
                continue;
            }
        }

        if (!msg.empty()) {
            if (mq_send(server_q, msg.c_str(), msg.size() + 1, 0) == -1) {
                // If server queue is closed or busy, print but keep running
                perror("mq_send");
            } else {
                // optional feedback
                // printf("[AUTO] %s\n", msg.c_str());
            }
        }

        // tiny randomized sleep to increase interleaving
        int ms = sleep_dist(rng);
        if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        else std::this_thread::yield();
    }
}

// new entry: spawn N worker threads to aggressively spam until Ctrl+C
void aggressive_spammer(mqd_t server_q, const std::string &client_qname, int workers = 8, int actions_per_worker = 20000) {
    std::vector<std::thread> workers_v;
    workers_v.reserve(workers);
    for (int i = 0; i < workers; ++i) {
        workers_v.emplace_back(aggressive_worker, server_q, client_qname, i+1, actions_per_worker);
    }

    // wait for them to finish or for running==false
    for (auto &th : workers_v) {
        if (th.joinable()) th.join();
    }
}

void heartbeat_thread(std::string qname)
{
    std::string beat_msg = "BEAT:" + qname;
    while(running)
    {
        // ส่งสัญญาณชีพทุก 5 วินาที
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        mqd_t hb_server_q = mq_open(CONTROL_Q, O_WRONLY);
        if (hb_server_q == (mqd_t)-1) {
            // ถ้า server ยังไม่ขึ้น ให้ลองใหม่รอบหน้า
            continue;
        }
        
        if (mq_send(hb_server_q, beat_msg.c_str(), beat_msg.size() + 1, 0) == -1) {
             // Error ส่งไม่สำเร็จ (server อาจจะปิด)
        }
        mq_close(hb_server_q);
    }
}

void handle_sigint(int) {
    running = false;
}

int main()
{   
    std::cout << "\nEnter your ChatName: ";
    std::getline(std::cin, client_name_global);
    if (client_name_global.empty()) {
        std::cout << "Name cannot be empty." << std::endl;
        return 1;
    }

    std::string client_qname = "/client_" + client_name_global;

    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = size_of_message;
    attr.mq_curmsgs = 0;

    // ลบ queue เดิมก่อนสร้างใหม่ (ถ้ามี)
    mq_unlink(client_qname.c_str());
    mqd_t client_q = mq_open(client_qname.c_str(), O_CREAT | O_RDONLY, 0666, &attr);
    if (client_q == (mqd_t)-1) {
        perror("mq_open client");
        return 1;
    }
    mq_close(client_q);

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    std::thread t(listen_queue, client_qname);
    std::thread(heartbeat_thread, client_qname).detach();

    mqd_t server_q = mq_open(CONTROL_Q, O_WRONLY);
    if (server_q == (mqd_t)-1) {
        perror("mq_open server_q");
        running = false;
        t.join();
        mq_unlink(client_qname.c_str());
        return 1;
    }

    // ส่ง Register message
    std::string reg_msg = "REGISTER:" + client_qname + ":" + client_name_global;
    mq_send(server_q, reg_msg.c_str(), reg_msg.size() + 1, 0);

    std::cout << "\nRegistered as " << client_name_global << std::endl;
    std::cout << "> " << std::flush;

    // ส่งข้อความรัว ๆ (กด Ctrl+C เพื่อหยุด)
    aggressive_spammer(server_q, client_qname, 6, 100000);

    mq_close(server_q);
    mq_unlink(client_qname.c_str());
    t.join();
    
    std::cout << "Disconnected." << std::endl;
    return 0;
}