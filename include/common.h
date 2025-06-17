#pragma once
#include<gtest/gtest.h>
#include<chrono>
#include<string>
#include<iostream>
#include<stdint.h>

#define VERSION 1.0.0

extern std::string g_process_name;

void SetCurrentProcessNameMy(const std::string& name);

std::string GetCurrentProcessNameMy();

class TimerForTest
{
public:
    using clock_t = std::chrono::high_resolution_clock;
    using duration_t = std::chrono::duration<double,std::milli>;

    TimerForTest(const std::string& func_name)
    :start_(clock_t::now())
    ,func_name_(func_name)
    {
    }

    ~TimerForTest(){
        auto end = clock_t::now();
        duration_t dur = end-start_;
        std::cout << "Func:"<<func_name_ <<" spent time:"<< dur.count() <<" ms"<< std::endl;
    }

private:
    const clock_t::time_point start_;
    std::string func_name_;
};

static int test(int a){
    std::cout << "testing ... a="<< a << std::endl;
    return a;
}