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



bool sendAndReceive(const std::string& portName, const std::string& message) {
    HANDLE hSerial = CreateFileA(portName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hSerial == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to open serial port\n";
        return false;
    }

    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(dcb);
    GetCommState(hSerial, &dcb);
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    SetCommState(hSerial, &dcb);

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 100;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    SetCommTimeouts(hSerial, &timeouts);

    PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

    DWORD bytesWritten = 0;
    BOOL success = WriteFile(hSerial, message.c_str(), message.length(), &bytesWritten, NULL);
    if (!success || bytesWritten != message.length()) {
        std::cerr << "Write error\n";
        CloseHandle(hSerial);
        return false;
    }

    Sleep(500);  // Wait for Pico to respond

    char buffer[256] = { 0 };
    DWORD bytesRead = 0;
    if (ReadFile(hSerial, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
        buffer[bytesRead] = '\0';
        std::cout << "Received: " << buffer << std::endl;
    }
    else {
        std::cerr << "No response.\n";
    }

    CloseHandle(hSerial);
    return true;
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

void saveBinaryTemperatureData(const std::vector<float>& temperatures, const std::string& filePath) {
    std::ofstream binFile(filePath, std::ios::binary);
    binFile.write(reinterpret_cast<const char*>(temperatures.data()), temperatures.size() * sizeof(float));
    binFile.close();
}

int main()
{
    std::cout << "Integration of Workswell camera\n";

    std::string port = "\\\\.\\COM3";  // COM3 escape format for Windows
    std::string msg = "ping\n";

    sendAndReceive(port, msg);


    std::string license_path = "C:/Users/stone/source/repos/Workswell_implementation_mk3/license_332C2309.wlic";
    wic::LicenseFile license(license_path.c_str());
    if (!license.isOk()) {
        std::cerr << "License invalid: " << license_path << std::endl;
        return 1;
    }

    auto wic = wic::findAndConnect(license);
    if (!wic) {
        std::cerr << "Could not connect WIC: " << license.serialNumber() << std::endl;
        return 2;
    }

    auto resolution = wic->getResolution();
    if (resolution.result[0] == 0 || resolution.result[1] == 0) {
        std::cerr << "Invalid resolution, core detection error." << std::endl;
        return 3;
    }

    auto defaultRes = wic->doDefaultWICSettings();
    if (defaultRes.status != wic::ResponseStatus::Ok) {
        std::cerr << "DoDefaultWICSettings: " << wic::responseStatusToStr(defaultRes.status) << std::endl;
        return 4;
    }

    auto coreTemp = wic->getCameraTemperature(wic::CameraTemperature::SensorTemp);
    std::cout << "Sensor temperature: " << coreTemp.result << std::endl;

    // Frame grabber setup
    auto grabber = wic->frameGrabber();
    grabber->setup();

    const size_t width = resolution.result[0];
    const size_t height = resolution.result[1];
    const size_t frameSize = width * height * sizeof(uint16_t);

    std::vector<std::vector<uint8_t>> rawFluxBuffers;   // Store raw radiometric flux
    std::vector<std::vector<uint8_t>> fluxBuffers;      // Store calibrated radiometric flux (Radiant flux)
    std::vector<std::vector<float>> tempBuffers;        // Store temperature data


    //-----------------------------------------------------------------------Count_frame---------------------------------------------------------------------
    size_t frame{ 0 };
    const int frame_number = 1;
    //-----------------------------------------------------------------------Count_frame---------------------------------------------------------------------


    auto error = wic::FrameGrabberError::None;
    std::cout << "Frame grabbing begins now..." << std::endl;

    while (frame < frame_number) {
        std::vector<uint8_t> buffer(frameSize);
        grabber->getBuffer(buffer.data(), error, 1000);

        if (error != wic::FrameGrabberError::None) {
            std::cerr << "Buffer error occurred on frame " << frame << std::endl;
            return 6;
        }

        // Save raw flux BEFORE calibration
        rawFluxBuffers.push_back(buffer); // Copy of raw buffer

        // Calibrate raw values (in-place)
        auto buffer16 = reinterpret_cast<uint16_t*>(buffer.data());
        wic->calibrateRawInplace(buffer16, frameSize / 2, (coreTemp.status == wic::ResponseStatus::Ok) ? coreTemp.result : 0);

        // Convert to temperature
        std::vector<float> temperatures(frameSize / 2);
        auto tempRes = wic->getCurrentTemperatureResolution();
        for (size_t i = 0; i < frameSize / 2; ++i) {
            temperatures[i] = wic::rawToCelsius(buffer16[i], tempRes);
        }

        // Store calibrated flux and temperatures
        fluxBuffers.push_back(buffer);
        tempBuffers.push_back(std::move(temperatures));

        ++frame;
    }


    std::string sessionTimestamp = currentDateTimeString();
    
    // Output directories
    //const std::string fluxDir = "E:/Thermography_True_setup/flux_data/";
    //const std::string tempDir = "E:/Thermography_True_setup/temperature_data/";
    
    std::string rootDir= "E:/Thermography_True_setup/Camera_measurments/recording_" + sessionTimestamp + "/";

    std::string binaryFluxDir = rootDir + "flux_data_binary/";
    std::string binaryTempDir = rootDir + "temperature_data_binary/";
    
    //const std::string binaryFluxDir = "E:/Thermography_True_setup/flux_data_binary/";
    //const std::string binaryTempDir = "E:/Thermography_True_setup/temperature_data_binary/";
    //const std::string rawFluxDir = "E:/Thermography_True_setup/raw_flux_data/";
    //std::filesystem::create_directories(fluxDir);
    //std::filesystem::create_directories(tempDir);
    std::filesystem::create_directories(binaryFluxDir);
    std::filesystem::create_directories(binaryTempDir);
    //std::filesystem::create_directories(rawFluxDir);


    // Save data
    for (size_t i = 0; i < frame_number; ++i) {

        std::string fluxPath = binaryFluxDir + "frame_" + std::to_string(i) + "_flux.bin";
        //std::string tempPath = binaryTempDir + "frame_" + std::to_string(i) + "_temperature.bin";

        saveBinaryData(fluxBuffers[i], fluxPath);
        //saveBinaryTemperatureData(tempBuffers[i], tempPath);
    }
    std::cout << "All frames have been captured and saved successfully." << std::endl;
    return 0;
}
