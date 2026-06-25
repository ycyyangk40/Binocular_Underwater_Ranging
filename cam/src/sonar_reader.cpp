#include "sonar_reader.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
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

#ifdef _WIN32
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
		0, nullptr,
		OPEN_EXISTING, 0, nullptr);

	if (h == INVALID_HANDLE_VALUE) return false;

	DCB dcb = {};
	dcb.DCBlength = sizeof(dcb);
	if (!GetCommState(h, &dcb)) { CloseHandle(h); return false; }

	dcb.BaudRate = baudRate;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;
	dcb.fBinary = TRUE;
	dcb.fDtrControl = DTR_CONTROL_DISABLE;
	dcb.fRtsControl = RTS_CONTROL_DISABLE;

	if (!SetCommState(h, &dcb)) { CloseHandle(h); return false; }

	COMMTIMEOUTS timeouts = {};
	timeouts.ReadIntervalTimeout = 50;
	timeouts.ReadTotalTimeoutConstant = 200;
	timeouts.ReadTotalTimeoutMultiplier = 10;
	timeouts.WriteTotalTimeoutConstant = 200;
	timeouts.WriteTotalTimeoutMultiplier = 10;

	if (!SetCommTimeouts(h, &timeouts)) { CloseHandle(h); return false; }

	PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

	fd_ = reinterpret_cast<intptr_t>(h);
	isWinHandle_ = true;
	return true;

#else
	int fd = ::open(portName.c_str(), O_RDWR | O_NOCTTY);
	if (fd < 0) {
		if (debug) fprintf(stderr, "[Sonar] open(%s) failed: %s\n", portName.c_str(), strerror(errno));
		return false;
	}

	struct termios tty = {};
	if (tcgetattr(fd, &tty) != 0) {
		if (debug) fprintf(stderr, "[Sonar] tcgetattr failed: %s\n", strerror(errno));
		::close(fd);
		return false;
	}

	speed_t speed = B115200;
	switch (baudRate) {
		case 9600:   speed = B9600;   break;
		case 19200:  speed = B19200;  break;
		case 38400:  speed = B38400;  break;
		case 57600:  speed = B57600;  break;
		case 115200: speed = B115200; break;
		case 230400: speed = B230400; break;
		case 460800: speed = B460800; break;
		case 921600: speed = B921600; break;
		default: speed = B115200; break;
	}
	cfsetospeed(&tty, speed);
	cfsetispeed(&tty, speed);

	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
	tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
	tty.c_cflag |= CLOCAL | CREAD;
	tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	tty.c_oflag &= ~OPOST;
	tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	tty.c_cc[VMIN] = 1;
	tty.c_cc[VTIME] = 2;

	if (tcsetattr(fd, TCSANOW, &tty) != 0) {
		if (debug) fprintf(stderr, "[Sonar] tcsetattr failed: %s\n", strerror(errno));
		::close(fd);
		return false;
	}

	tcflush(fd, TCIOFLUSH);
	fd_ = fd;
	isWinHandle_ = false;
	return true;
#endif
}

void SonarReader::close() {
#ifdef _WIN32
	if (isWinHandle_ && fd_ != 0) {
		CloseHandle(reinterpret_cast<HANDLE>(fd_));
	}
#else
	if (!isWinHandle_ && fd_ >= 0) {
		::close(static_cast<int>(fd_));
	}
#endif
	fd_ = 0;
	isWinHandle_ = false;
}

bool SonarReader::isOpen() const {
#ifdef _WIN32
	return isWinHandle_ && fd_ != 0;
#else
	return !isWinHandle_ && fd_ >= 0;
#endif
}

bool SonarReader::writeBytes(const std::vector<uint8_t>& data) {
#ifdef _WIN32
	if (!isWinHandle_ || fd_ == 0) return false;
	DWORD written = 0;
	if (!WriteFile(reinterpret_cast<HANDLE>(fd_), data.data(),
		static_cast<DWORD>(data.size()), &written, nullptr)) {
		return false;
	}
	return written == data.size();
#else
	if (isWinHandle_ || fd_ < 0) return false;
	ssize_t n = ::write(static_cast<int>(fd_), data.data(), data.size());
	if (n < 0) {
		if (debug) fprintf(stderr, "[Sonar] write failed: %s\n", strerror(errno));
		return false;
	}
	return static_cast<size_t>(n) == data.size();
#endif
}

bool SonarReader::readBytes(std::vector<uint8_t>& data, size_t expectedLen, int timeoutMs) {
	data.clear();
	data.resize(expectedLen);

	size_t totalRead = 0;
	auto start = std::chrono::steady_clock::now();

	while (totalRead < expectedLen) {
		size_t remaining = expectedLen - totalRead;

#ifdef _WIN32
		if (!isWinHandle_ || fd_ == 0) return false;
		DWORD bytesRead = 0;
		if (!ReadFile(reinterpret_cast<HANDLE>(fd_), data.data() + totalRead,
			static_cast<DWORD>(remaining), &bytesRead, nullptr)) {
			return false;
		}
#else
		if (isWinHandle_ || fd_ < 0) return false;
		int fd = static_cast<int>(fd_);

		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(fd, &readfds);

		auto elapsed1 = std::chrono::steady_clock::now() - start;
		int remainMs = timeoutMs - static_cast<int>(
			std::chrono::duration_cast<std::chrono::milliseconds>(elapsed1).count());
		if (remainMs < 0) remainMs = 0;

		struct timeval tv = { remainMs / 1000, (remainMs % 1000) * 1000 };
		int selRet = select(fd + 1, &readfds, nullptr, nullptr, &tv);
		if (selRet < 0) return false;
		if (selRet == 0) break;

		ssize_t bytesRead = ::read(fd, data.data() + totalRead, remaining);
		if (bytesRead < 0) return false;
		if (bytesRead == 0) break;
#endif

		totalRead += bytesRead;

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

	if (!isOpen()) {
		out.error = "com_not_open";
		return false;
	}

#ifdef _WIN32
	PurgeComm(reinterpret_cast<HANDLE>(fd_), PURGE_RXCLEAR | PURGE_TXCLEAR);
#else
	tcflush(static_cast<int>(fd_), TCIOFLUSH);
#endif

	std::vector<uint8_t> req = {
		slaveAddr_,
		0x03,
		0x00, 0x10,
		0x00, 0x01,
		0x00, 0x00
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
