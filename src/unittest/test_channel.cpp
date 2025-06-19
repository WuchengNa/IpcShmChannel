#include "common.h"

#include "IpcShmChannel.h"
#include <future>

#define SHM_NAME "TEST_IPCCHN"


TEST(api, stress_test) {
    // 判断启动参数，决定角色
    bool is_server = false;
    auto args = ::testing::internal::GetArgvs();
    for (const auto& arg : args) {
        if (arg.find("server") != std::string::npos) {
            is_server = true;
            break;
        }
        if (arg.find("client") != std::string::npos) {
            is_server = false;
            break;
        }
    }

    IpcShmChannel channel(is_server ? IpcShmChannel::Role::SERVER : IpcShmChannel::Role::CLIENT, SHM_NAME);
    const int MSG_COUNT = 10000;
    std::atomic<int> recv_count{0};
    static std::promise<bool> connect;
    auto fu_connect = connect.get_future();
    bool saidHello = false;

    channel.SetRecvCallback([&](IpcShmChannel::EventType evt, int send_pid, const char* data, size_t size) {
        if (evt == IpcShmChannel::EventType::start_listen) {
            connect.set_value(true);
            std::cout << (is_server ? "[Server] " : "[Client] ") << "start_listen " << data << std::endl;
            return;
        }
        else if (evt == IpcShmChannel::EventType::client_hello || evt == IpcShmChannel::EventType::server_hello) {
            saidHello = true;
            std::cout << (is_server ? "[Server] " : "[Client] ") << " recv hello!" << std::endl;
            return;
        }
        else if (evt == IpcShmChannel::EventType::client_byebye || evt == IpcShmChannel::EventType::server_byebye) {
            std::cout << (is_server ? "[Server] " : "[Client] ") << "recv byebye!" << std::endl;
            return;
        }
        else {
            std::string dataStr(data, size);
            std::cout << (is_server ? "[Server] " : "[Client] ") << "Received message, type:" << (short)evt << " from pid " << send_pid << ": " << dataStr << std::endl;
            recv_count++;
        }
    });

    std::cout << (is_server ? "[Server] " : "[Client] ") << "Starting..." << std::endl;
    channel.Start();
    auto con = fu_connect.get();

    while (!saidHello)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    if (getchar() == 'q')
        channel.Stop();

    auto start_time = std::chrono::steady_clock::now();
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::thread send_thread([&]() {
        start_time = std::chrono::steady_clock::now();
        for (int i = 0; i < MSG_COUNT; i++) {
            std::string msg = std::string(is_server ? "Server" : "Client") + " Message #" + std::to_string(i);
            channel.SendTextMsg(msg.c_str(), msg.length());
        }
        end_time = std::chrono::steady_clock::now();
        channel.Stop();
    });

    send_thread.join();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    EXPECT_EQ(recv_count, MSG_COUNT);

    std::cout << "\n" << (is_server ? "[Server] " : "[Client] ") << "Stress Test Results:" << std::endl;
    std::cout << "Messages sent: " << MSG_COUNT << std::endl;
    std::cout << "Messages received: " << recv_count << std::endl;
    std::cout << "Time: " << duration.count() << "ms" << std::endl;
    std::cout << "Messages per milli second: " << (duration.count() > 0 ? static_cast<double>(MSG_COUNT) / duration.count() : 0) << std::endl;
}
