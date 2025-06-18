#include "common.h"

#include "IpcShmChannel.h"

#define SHM_NAME "TEST_IPCCHN"

TEST(api,clear)
{
    std::string up = std::string(SHM_NAME) + "_up";
    std::string down = std::string(SHM_NAME) + "_down";
    boost::interprocess::shared_memory_object::remove(up.c_str());
    boost::interprocess::shared_memory_object::remove(down.c_str());
}

TEST(api,server)
{
        IpcShmChannel server(Role::SERVER, SHM_NAME);
        server.SetRecvCallback([&](int send_pid, const char* data, size_t size) {
            std::string dataStr(data, size);
            std::cout <<"Server Recv:" << dataStr << std::endl;
        });
        server.Start();
        // Server 发送消息
        while(getchar() != 'q'){
            server.Send("Hello from Server", 17);
        }    
        server.Send("ByeBye from Server", 18);
        server.Stop();
}

TEST(api,client)
{
        IpcShmChannel client(Role::CLIENT, SHM_NAME);
        client.SetRecvCallback([&](int send_pid, const char* data, size_t size) {
            std::string dataStr(data, size);
            std::cout <<"Client Recv:" << dataStr << std::endl;
        });
        client.Start();
        // Client 发送消息
        while(getchar() != 'q'){
            client.Send("Hello from Client", 17);
        }  
        client.Send("ByeBye from Client", 18);
        client.Stop();
}