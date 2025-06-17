#include "common.h"

#include "IpcShmChannel.h"

#define SHM_NAME "TEST_IPCCHN"

TEST(api,server)
{
        IpcShmChannel server(Role::SERVER, SHM_NAME);
        server.SetRecvCallback([&](const char* data, size_t size) {
            std::string dataStr(data, size);
            std::cout <<"Server Recv:" << dataStr << std::endl;
        });

        // Server 发送消息
        while(getchar() != 'q'){
            server.Send("Hello from Server", 17);
        }    
}

TEST(api,client)
{
        IpcShmChannel client(Role::CLIENT, SHM_NAME);
        client.SetRecvCallback([&](const char* data, size_t size) {
            std::string dataStr(data, size);
            std::cout <<"Client Recv:" << dataStr << std::endl;
        });

        // Server 发送消息
        while(getchar() != 'q'){
            client.Send("Hello from Client", 17);
        }  
}