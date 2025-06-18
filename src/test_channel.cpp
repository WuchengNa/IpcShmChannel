#include "common.h"

#include "IpcShmChannel.h"
#include <future>

#define SHM_NAME "TEST_IPCCHN"

TEST(api,clear)
{
    std::string up = std::string(SHM_NAME) + "_up";
    std::string down = std::string(SHM_NAME) + "_down";
    boost::interprocess::shared_memory_object::remove(up.c_str());
    boost::interprocess::shared_memory_object::remove(down.c_str());
}

TEST(api, server) {
    IpcShmChannel server(IpcShmChannel::Role::SERVER, SHM_NAME);
    const int MSG_COUNT = 100;
    std::atomic<int> recv_count{0};
    static std::promise<bool> connect;
    auto fu_connect = connect.get_future();
    
    server.SetRecvCallback([&](IpcShmChannel::EventType evt, int send_pid, const char* data, size_t size) {
        if (evt == IpcShmChannel::EventType::start_listen)
        {
            connect.set_value(true);
            std::cout << "[Server] Received client hello" << std::endl;
            return;
        }
        else if (evt == IpcShmChannel::EventType::client_hello)
        {
            std::cout << "[Server] recv client_hello !" << std::endl;
            return;
        }
        else if (evt == IpcShmChannel::EventType::client_byebye) {
            std::cout << "[Server] Received client byebye" << std::endl;
            return;
        }
        else {
            std::string dataStr(data, size);
            std::cout << "[Server] Received message from pid " << send_pid << ": " << dataStr << std::endl;
            recv_count++;
        }
    });

    std::cout << "[Server] Starting..." << std::endl;
    server.Start();
    auto con = fu_connect.get();
    auto start_time = std::chrono::steady_clock::now();
    auto end_time = std::chrono::steady_clock::now();

    std::thread send_thread([&]() {
        for(int i = 0; i < MSG_COUNT; i++) {
            std::string msg = "Server Message #" + std::to_string(i);
            //std::cout << "[Server] Sending: " << msg << std::endl;
            server.SendTextMsg(msg.c_str(), msg.length());
        }
        end_time = std::chrono::steady_clock::now();
        server.Stop();
    });    

    send_thread.join();
    //std::this_thread::sleep_for(std::chrono::seconds(2));
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);


    EXPECT_EQ(recv_count, MSG_COUNT);

    std::cout << "\n[Server] Stress Test Results:" << std::endl;
    std::cout << "Messages sent: " << MSG_COUNT << std::endl;
    std::cout << "Messages received: " << recv_count << std::endl;
    std::cout << "Time: " << duration.count() << "ms" << std::endl;
    std::cout << "Messages per milli second  : " << static_cast<double>(MSG_COUNT) / duration.count() << std::endl;
}

TEST(api, client) {
    IpcShmChannel client(IpcShmChannel::Role::CLIENT, SHM_NAME);
    const int MSG_COUNT = 100;
    std::atomic<int> recv_count{0};
    static std::promise<bool> connect;
    auto fu_connect = connect.get_future();
    client.SetRecvCallback([&](IpcShmChannel::EventType evt,int send_pid, const char* data, size_t size) {
        if (evt == IpcShmChannel::EventType::start_listen)
        {
            connect.set_value(true);
            std::cout << "[client] connect !" << std::endl;
            return;
        }
        else if (evt == IpcShmChannel::EventType::server_hello)
        {
            std::cout << "[client] recv server_hello !" << std::endl;
            return;
        }
        else if (evt == IpcShmChannel::EventType::server_byebye) {
            std::cout << "[client] Received client byebye" << std::endl;
            return;
        }
        else {
            std::string dataStr(data, size);
            std::cout << "[Client] Received message from pid " << send_pid << ": " << dataStr << std::endl;
            recv_count++;
        }
    });

    std::cout << "[Client] Starting..." << std::endl;
    client.Start();
    auto con = fu_connect.get();
    auto start_time = std::chrono::steady_clock::now();
    auto end_time = std::chrono::steady_clock::now();
    std::thread send_thread([&]() {
        for(int i = 0; i < MSG_COUNT; i++) {
            std::string msg = "Client Message #" + std::to_string(i);
            //std::cout << "[Client] Sending: " << msg << std::endl;
            client.SendTextMsg(msg.c_str(), msg.length());
        }
        end_time = std::chrono::steady_clock::now();
        client.Stop();
    });

    send_thread.join();
    //std::this_thread::sleep_for(std::chrono::seconds(2));
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);


    EXPECT_EQ(recv_count, MSG_COUNT);

    std::cout << "\n[Server] Stress Test Results:" << std::endl;
    std::cout << "Messages sent: " << MSG_COUNT << std::endl;
    std::cout << "Messages received: " << recv_count << std::endl;
    std::cout << "Time: " << duration.count() << "ms" << std::endl;
    std::cout << "Messages per milli second  : " << static_cast<double>(MSG_COUNT) / duration.count() << std::endl;
}

TEST(api, run_stress_tests) {
    auto proName = GetCurrentProcessNameMy();
    auto clientProName = proName + " --gtest_filter=api.stress_test_client";
    std::system(clientProName.c_str());

    auto serverProName = proName + " --gtest_filter=api.stress_test_server";
    std::system(serverProName.c_str());

}