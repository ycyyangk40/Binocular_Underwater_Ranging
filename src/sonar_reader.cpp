#include "sonar_reader.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <chrono>
#include <cstdio>
#include <sstream>
#include <thread>

SonarReader::SonarReader() = default;

SonarReader::~SonarReader() {
	close();
}

bool SonarReader::open(const std::string& portName, int baudRate, uint8_t slaveAddr) {
	close();
	slaveAddr_ = slaveAddr;

	std::string deviceName = portName;
	if (portName.rfind("COM", 0) == 0 || portName.rfind("com", 0) == 0) {
		int num = std::stoi(portName.substr(3));
		if (num >= 10) {
			deviceName = "\\\\.\\" + portName;
		}
	}

	HANDLE h = CreateFileA(
		deviceName.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0,
		nullptr,
		OPEN_EXISTING,
		0,
		nullptr);

	if (h == INVALID_HANDLE_VALUE) {
		return false;
	}

	DCB dcb = {};
	dcb.DCBlength = sizeof(dcb);
	if (!GetCommState(h, &dcb)) {
		CloseHandle(h);
		return false;
	}

	dcb.BaudRate = baudRate;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;
	dcb.fBinary = TRUE;
	dcb.fDtrControl = DTR_CONTROL_DISABLE;
	dcb.fRtsControl = RTS_CONTROL_DISABLE;

	if (!SetCommState(h, &dcb)) {
		CloseHandle(h);
		return false;
	}

	COMMTIMEOUTS timeouts = {};
	timeouts.ReadIntervalTimeout = 50;
	timeouts.ReadTotalTimeoutConstant = 200;
	timeouts.ReadTotalTimeoutMultiplier = 10;
	timeouts.WriteTotalTimeoutConstant = 200;
	timeouts.WriteTotalTimeoutMultiplier = 10;

	if (!SetCommTimeouts(h, &timeouts)) {
		CloseHandle(h);
		return false;
	}

	PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

	handle_ = h;
	return true;
}

void SonarReader::close() {
	if (handle_) {
		CloseHandle(static_cast<HANDLE>(handle_));
		handle_ = nullptr;
	}
}

bool SonarReader::isOpen() const {
	return handle_ != nullptr;
}

bool SonarReader::writeBytes(const std::vector<uint8_t>& data) {
	if (!handle_) return false;

	DWORD written = 0;
	if (!WriteFile(static_cast<HANDLE>(handle_), data.data(),
		static_cast<DWORD>(data.size()), &written, nullptr)) {
		return false;
	}
	return written == data.size();
}

bool SonarReader::readBytes(std::vector<uint8_t>& data, size_t expectedLen, int timeoutMs) {
	if (!handle_) return false;

	data.clear();
	data.resize(expectedLen);

	DWORD totalRead = 0;
	auto start = std::chrono::steady_clock::now();

	while (totalRead < expectedLen) {
		DWORD bytesRead = 0;
		if (!ReadFile(static_cast<HANDLE>(handle_), data.data() + totalRead,
			static_cast<DWORD>(expectedLen - totalRead), &bytesRead, nullptr)) {
			return false;
		}

		totalRead += bytesRead;

		if (totalRead >= expectedLen) break;

		auto elapsed = std::chrono::steady_clock::now() - start;
		if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeoutMs) {
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	data.resize(totalRead);
	return totalRead == expectedLen;
}

uint16_t SonarReader::modbusCrc16(const uint8_t* data, size_t len) {
	uint16_t crc = 0xFFFF;
	for (size_t i = 0; i < len; ++i) {
		crc ^= data[i];
		for (int j = 0; j < 8; ++j) {
			if (crc & 0x0001) {
				crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001);
			} else {
				crc = static_cast<uint16_t>(crc >> 1);
			}
		}
	}
	return crc;
}

bool SonarReader::checkModbusCrc(const std::vector<uint8_t>& data) {
	if (data.size() < 3) return false;

	uint16_t expected = modbusCrc16(data.data(), data.size() - 2);
	uint16_t actual = static_cast<uint16_t>(data[data.size() - 2])
		| (static_cast<uint16_t>(data[data.size() - 1]) << 8);

	return expected == actual;
}

bool SonarReader::readOnce(SonarData& out) {
	out = SonarData{};

	if (!handle_) {
		out.error = "com_not_open";
		return false;
	}

	PurgeComm(static_cast<HANDLE>(handle_), PURGE_RXCLEAR | PURGE_TXCLEAR);

	std::vector<uint8_t> req = {
		slaveAddr_,
		0x03,
		0x00,
		0x10,
		0x00,
		0x01,
		0x00,
		0x00
	};

	uint16_t crc = modbusCrc16(req.data(), 6);
	req[6] = crc & 0xFF;
	req[7] = (crc >> 8) & 0xFF;

	if (debug) {
		fprintf(stderr, "[Sonar Debug] TX (%zu bytes): ", req.size());
		for (uint8_t b : req) fprintf(stderr, "%02X ", b);
		fprintf(stderr, "\n");
	}

	if (!writeBytes(req)) {
		out.error = "write_failed";
		return false;
	}

	std::vector<uint8_t> resp;
	readBytes(resp, 7);

	if (debug) {
		fprintf(stderr, "[Sonar Debug] RX (%zu bytes): ", resp.size());
		for (uint8_t b : resp) fprintf(stderr, "%02X ", b);
		fprintf(stderr, "\n");
	}

	if (resp.empty()) {
		out.error = "timeout";
		return false;
	}

	if (resp.size() < 7) {
		out.error = "short_frame";
		return false;
	}

	if (resp[0] != slaveAddr_) {
		out.error = "addr_mismatch";
		return false;
	}

	if (resp[1] != 0x03) {
		if (resp[1] & 0x80) {
			out.error = "exception_code_" + std::to_string(resp[2]);
		} else {
			out.error = "bad_func_code";
		}
		return false;
	}

	if (resp[2] != 0x02) {
		out.error = "bad_data_len";
		return false;
	}

	if (!checkModbusCrc(resp)) {
		out.error = "crc_error";
		return false;
	}

	uint16_t raw = (static_cast<uint16_t>(resp[3]) << 8) | resp[4];
	out.valid = true;
	out.distance_mm = static_cast<double>(raw) * 0.1;
	out.distance_m = out.distance_mm / 1000.0;
	return true;
}
