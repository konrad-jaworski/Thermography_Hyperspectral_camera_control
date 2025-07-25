#include <calibrationdata.h>
#include <camerafinder.h>
#include <framegrabber.h>
#include <wic.h>
#include <iostream>
#include <string>
#include <fstream>
#include <algorithm>
#include <vector>
#include <filesystem>
#include <Windows.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <map>


// Open serial port, return handle or INVALID_HANDLE_VALUE on failure
HANDLE openSerialPort(const std::string& portName) {
    HANDLE hSerial = CreateFileA(portName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hSerial == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to open serial port\n";
        return INVALID_HANDLE_VALUE;
    }

    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(hSerial, &dcb)) {
        std::cerr << "GetCommState failed\n";
        CloseHandle(hSerial);
        return INVALID_HANDLE_VALUE;
    }

    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;

    if (!SetCommState(hSerial, &dcb)) {
        std::cerr << "SetCommState failed\n";
        CloseHandle(hSerial);
        return INVALID_HANDLE_VALUE;
    }

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 5000; // increased timeout for read
    timeouts.ReadTotalTimeoutMultiplier = 10;
    if (!SetCommTimeouts(hSerial, &timeouts)) {
        std::cerr << "SetCommTimeouts failed\n";
        CloseHandle(hSerial);
        return INVALID_HANDLE_VALUE;
    }

    PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

    return hSerial;
}

// Send message to serial port, return true if full message sent
bool sendMessage(HANDLE hSerial, const std::string& message) {
    DWORD bytesWritten = 0;
    BOOL success = WriteFile(hSerial, message.c_str(), (DWORD)message.length(), &bytesWritten, NULL);
    if (!success || bytesWritten != message.length()) {
        std::cerr << "Write error\n";
        return false;
    }
    return true;
}

// Receive response from serial port, blocking with timeout; returns true if something read into response
bool receiveResponse(HANDLE hSerial, std::string& response, int timeout_ms = 1000) {
    char buffer[256] = { 0 };
    DWORD bytesRead = 0;
    auto start = std::chrono::steady_clock::now();

    response.clear();

    while (true) {
        BOOL success = ReadFile(hSerial, buffer, sizeof(buffer) - 1, &bytesRead, NULL);
        if (!success) {
            std::cerr << "ReadFile error\n";
            return false;
        }
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            response += buffer;

            // If response contains "pong", we can return early
            if (response.find("pong") != std::string::npos) {
                return true;
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > timeout_ms) {
            // Timeout
            break;
        }

        Sleep(10); // small sleep to avoid busy wait
    }
    return !response.empty();
}

std::string currentDateTimeString() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);

    std::tm now_tm;
    localtime_s(&now_tm, &now_c);  // safer alternative to localtime

    std::stringstream ss;
    ss << std::put_time(&now_tm, "%Y-%m-%d_%H-%M-%S");
    return ss.str();
}

void saveBinaryData(const std::vector<uint8_t>& buffer, const std::string& filePath) {
    std::ofstream binFile(filePath, std::ios::binary);
    binFile.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    binFile.close();
}

void saveBinaryDataCalibrated(const std::vector<uint16_t>& data, const std::string& filePath) {
    std::ofstream binFile(filePath, std::ios::binary);
    binFile.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(uint16_t));
    binFile.close();
}

void saveBinaryTemperatureData(const std::vector<float>& temperatures, const std::string& filePath) {
    std::ofstream binFile(filePath, std::ios::binary);
    binFile.write(reinterpret_cast<const char*>(temperatures.data()), temperatures.size() * sizeof(float));
    binFile.close();
}

int main()
{
    std::cout << "Integration of Workswell camera and Pico board\n";

    // Open serial port once
    std::string port = "\\\\.\\COM3";  // COM3 escape format for Windows
    HANDLE hSerial = openSerialPort(port);
    if (hSerial == INVALID_HANDLE_VALUE) {
        return 1;  // Failed to open serial port
    }

    // Initialize camera license
    std::string license_path = "C:/Users/stone/source/repos/Workswell_implementation_mk3/license_332C2309.wlic";
    wic::LicenseFile license(license_path.c_str());
    if (!license.isOk()) {
        std::cerr << "License invalid: " << license_path << std::endl;
        CloseHandle(hSerial);
        return 2;
    }

    // Initialize camera connection
    auto wic = wic::findAndConnect(license);
    if (!wic) {
        std::cerr << "Could not connect WIC: " << license.serialNumber() << std::endl;
        CloseHandle(hSerial);
        return 3;
    }

    // Setting up initial WIC camera setting
    auto defaultRes = wic->doDefaultWICSettings();
    if (defaultRes.status != wic::ResponseStatus::Ok) {
        std::cerr << "DoDefaultWICSettings: " << wic::responseStatusToStr(defaultRes.status) << std::endl;
        return 5;
    }

    // Setting Thermal range of the camera
    auto res = wic->setRange(wic::Range::High);
    if (res != wic::ResponseStatus::Ok) {
        std::cerr << "Failed to set range." << std::endl;
    }

    auto tempRes = wic->getCurrentTemperatureResolution();
    std::cout << "Current temperature resolution: " << static_cast<int>(tempRes) << std::endl;
    
    // Frame grabber setup
    auto grabber = wic->frameGrabber();
    grabber->setup();

    // Resolution of the camera
    auto resolution = wic->getResolution();
    if (resolution.result[0] == 0 || resolution.result[1] == 0) {
        std::cerr << "Invalid resolution, core detection error." << std::endl;
        CloseHandle(hSerial);
        return 4;
    }

    // Camera sensor temperature
    auto coreTemp = wic->getCameraTemperature(wic::CameraTemperature::SensorTemp);
    std::cout << "Sensor temperature: " << coreTemp.result << std::endl;

    const size_t width = resolution.result[0];
    const size_t height = resolution.result[1];
    const size_t frameSize = width * height * sizeof(uint16_t);

    std::map<std::string, std::vector<uint8_t>> framesDict; // store frames with index keys

    // Create root output folder with timestamp
    std::string sessionTimestamp = currentDateTimeString();
    std::string rootDir = "E:/Thermography_True_setup/Camera_measurments/recording_" + sessionTimestamp + "/";
    std::filesystem::create_directories(rootDir);

    std::cout << "Starting main ping-pong loop\n";

    // Loop from 0_0 to 7_7
    for (int i = 0; i <= 7; ++i) {
        for (int j = 0; j <= 7; ++j) {
            std::string index = std::to_string(i) + "_" + std::to_string(j);
            std::string pingMsg = "ping " + index + "\n";

            std::cout << "Sending: " << pingMsg;
            if (!sendMessage(hSerial, pingMsg)) {
                std::cerr << "Failed to send ping for index " << index << "\n";
                continue;
            }

            std::string response;
            if (!receiveResponse(hSerial, response, 1000)) {
                std::cerr << "No pong response for index " << index << "\n";
                continue;
            }

            std::cout << "Received pong for index " << index << "\n";

            // Grab one frame from camera
            std::vector<uint8_t> buffer(frameSize);
            wic::FrameGrabberError error = wic::FrameGrabberError::None;

            // In buffer are stored Rad14 data from the camera
            grabber->getBuffer(buffer.data(), error, 1000);
            if (error != wic::FrameGrabberError::None) {
                std::cerr << "Frame grabber error at index " << index << std::endl;
                continue;
            }

            // Correction of raw data due to sensor temperature
            auto buffer16 = reinterpret_cast<uint16_t*>(buffer.data());
            auto coreTemp = wic->getCameraTemperature(wic::CameraTemperature::SensorTemp);
            wic->calibrateRawInplace(buffer16, frameSize / 2, (coreTemp.status == wic::ResponseStatus::Ok) ? coreTemp.result : 0);

            // Saving calibrated temperature in vector form to save later
            std::vector<uint16_t> calibratedData(buffer16, buffer16 + (frameSize / 2));


            // Saving temperature measurments
            std::vector<float> temperatures(frameSize / 2);
            auto tempRes = wic->getCurrentTemperatureResolution();
            for (size_t k = 0; k < frameSize / 2; ++k) {
                temperatures[k] = wic::rawToCelsius(buffer16[k], tempRes);
            }

            // Store frame in dictionary
            framesDict[index] = buffer;

            // Save frame immediately to file with timestamp and index
            std::string filename = rootDir + "frame_" + index + "_" + "_flux.bin";
            saveBinaryData(buffer, filename);

            std::string calPath = rootDir + "calibrated_data_binary/frame_" + std::to_string(i) + "_" + std::to_string(j) + "_calibrated.bin";
            std::filesystem::create_directories(rootDir + "calibrated_data_binary");
            saveBinaryDataCalibrated(calibratedData, calPath);

            // Save temperature data
            std::string tempPath = rootDir + "temperature_data_binary/frame_" + std::to_string(i) + "_" + std::to_string(j) + "_temperature.bin";
            std::filesystem::create_directories(rootDir + "temperature_data_binary");
            saveBinaryTemperatureData(temperatures, tempPath);

            std::cout << "Saved frame for index " << index << " to " << filename << std::endl;
        }
    }

    CloseHandle(hSerial);

    std::cout << "All frames processed and saved." << std::endl;
    return 0;
}