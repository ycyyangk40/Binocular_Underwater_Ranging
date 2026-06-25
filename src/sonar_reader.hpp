#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SonarData {
	bool valid = false;
	double distance_mm = 0.0;
	double distance_m = 0.0;
	std::string error;
};

class SonarReader {
public:
	SonarReader();
	~SonarReader();

	bool open(const std::string& portName, int baudRate = 115200, uint8_t slaveAddr = 1);
	void close();
	bool isOpen() const;

	bool readOnce(SonarData& out);

	bool debug = false;

private:
	bool writeBytes(const std::vector<uint8_t>& data);
	bool readBytes(std::vector<uint8_t>& data, size_t expectedLen, int timeoutMs = 300);

	static uint16_t modbusCrc16(const uint8_t* data, size_t len);
	static bool checkModbusCrc(const std::vector<uint8_t>& data);

	void* handle_ = nullptr;
	uint8_t slaveAddr_ = 1;
};
