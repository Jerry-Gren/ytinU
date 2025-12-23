#pragma once
#include <chrono>
#include <iostream>
#include <string>

class ScopedTimer {
public:
    ScopedTimer(const std::string& name) : _name(name) {
        _start = std::chrono::high_resolution_clock::now();
    }
    
    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        double duration = std::chrono::duration<double, std::milli>(end - _start).count();
        std::cout << "[Timer] " << _name << " took " << duration << " ms" << std::endl;
    }
private:
    std::string _name;
    std::chrono::time_point<std::chrono::high_resolution_clock> _start;
};