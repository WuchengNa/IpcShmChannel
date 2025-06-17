#include <iostream>
#include <string>
#include "common.h"
static std::string g_process_name;

void SetCurrentProcessNameMy(const std::string& name){
    g_process_name = name;
}

std::string GetCurrentProcessNameMy()
{
    return g_process_name;
}

int main(int argc, char* argv[])
{
    if(argc == 2)
    {
        testing::GTEST_FLAG(filter) = argv[1];
    }else{
        testing::GTEST_FLAG(filter) = "*";
    }


    SetCurrentProcessNameMy(argv[0]);
    testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}