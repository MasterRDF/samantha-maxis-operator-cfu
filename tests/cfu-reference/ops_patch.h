#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace mwcfops {
enum class Fault { None, Allocation, CodeProtection, ImageProtection };
struct Input {
    std::vector<uint64_t> operators;
    std::vector<uint64_t> skins;
};
struct Result {
    uint8_t* allocation = nullptr;
    size_t allocationBytes = 0;
    size_t dataBytes = 0;
    uint8_t* code = nullptr;
    LONG* counter = nullptr;
};
bool Install(uint8_t* image, size_t imageBytes, const Input& input, Result& result, std::string& error, Fault fault = Fault::None);
}
