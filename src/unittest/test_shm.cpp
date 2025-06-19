#include "common.h"

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <cstring>
#include <cstdlib>
#include <string>

#include <nlohmann/json.hpp>

using namespace boost::interprocess;

#define TEST_SHM_NAME "SHMMMMMMMMMM"


TEST(shm, server)
{
       //Remove shared memory on construction and destruction
      struct shm_remove
      {
         shm_remove() { shared_memory_object::remove(TEST_SHM_NAME); }
         ~shm_remove(){ shared_memory_object::remove(TEST_SHM_NAME); }
      } remover;
      //<-
      (void)remover;

    //Create a shared memory object.
    shared_memory_object shm (create_only, TEST_SHM_NAME, read_write);

    //Set size
    shm.truncate(1000);

    //Map the whole shared memory in this process
    mapped_region region(shm, read_write);

    nlohmann::json msg;
    msg["id"] = 123;
    msg["msg"] = "hello i m server";
    msg["type"] = 1;
    msg["data"] = {1,2,3};
    msg["strData"] = {"a","fdsa","dfsa"};
    msg["isMsg"] = true;

    auto msgStr = msg.dump();
    char* data = static_cast<char*> (region.get_address());
    //Write all the memory to 1
    std::memset(region.get_address(), 0, region.get_size());
    std::memcpy(region.get_address(), msgStr.c_str(), msgStr.size());

    auto proName = GetCurrentProcessNameMy();
    proName += " --gtest_filter=shm.client";
    std::system(proName.c_str());

    getchar();
}

TEST(shm,client)
{
    shared_memory_object shm (open_only, TEST_SHM_NAME, read_only);

    //Map the whole shared memory in this process
    mapped_region region(shm, read_only);

    //read json string 
    std::stringstream jsonStr;
    char *mem = static_cast<char*>(region.get_address());
    for(std::size_t i = 0; i < region.get_size(); ++i)
    {
        jsonStr <<*mem;
        if(*mem == '\0')
            break;
        *mem++;
    } 
    nlohmann::json json = nlohmann::json::parse(jsonStr);
    int id = json["id"];
    std::string msg = json["msg"];
    int type = json["type"];
    auto dataArry = json["data"];
    std::vector<int> data;
    for(auto item:dataArry.array())
    {
        data.push_back(item);
    }
    bool isMsg = json["isMsg"];

    std::cout <<" Msg id:" << id
            <<" msg:" << msg
            <<" type:"<<type<<std::endl;
     
} 