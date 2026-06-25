#include "mode_sonar.hpp"
#include "sonar_reader.hpp"

#include <chrono>
#include <iostream>
#include <thread>

int runSonarTest(const std::string& portName, int baudRate, int slaveAddr, bool debug) {
	std::cout << "[Sonar] Port: " << portName << "\n";
	std::cout << "[Sonar] Baud: " << baudRate << "\n";
	std::cout << "[Sonar] Addr: " << slaveAddr << "\n";

	SonarReader reader;
	reader.debug = debug;
	if (!reader.open(portName, baudRate, static_cast<uint8_t>(slaveAddr))) {
		std::cout << "[Sonar] com_open_failed\n";
		return 1;
	}
	std::cout << "[Sonar] Open " << portName << " success\n";

	while (true) {
		SonarData data;
		if (reader.readOnce(data)) {
			std::cout << "[Sonar] distance = " << data.distance_mm
				<< " mm, " << data.distance_m << " m\n";
		} else {
			std::cout << "[Sonar] " << data.error << "\n";
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
	}

	return 0;
}
