#include <iostream>
#include <string>
#include <vector>
#include <boost/asio.hpp>
#include <boost/asio/serial_port.hpp>
#include <boost/system/error_code.hpp>

// Define your VID and PID here
const int VENDOR_ID = 0x1A86;
const int PRODUCT_ID = 0x7523;

std::string find_serial_port() {
    boost::asio::io_context io;
    boost::asio::serial_port port(io);
    boost::system::error_code ec;

    // This is a placeholder for iterating through ports, as Boost.Asio
    // does not provide a direct way to list serial ports.
    // A platform-specific method is still required to get a list of port names.
    // For now, we will try a common range of port names.
    std::vector<std::string> port_names;
    for (int i = 0; i < 16; ++i) {
        port_names.push_back("COM" + std::to_string(i));
    }
    // for (int i = 0; i < 16; ++i) {
    //     port_names.push_back("/dev/ttyUSB" + std::to_string(i));
    //     port_names.push_back("/dev/ttyACM" + std::to_string(i));
    // }

    for (const auto& port_name : port_names) {
        port.open(port_name, ec);
        if (ec) {
            std::cerr << "Error opening " << port_name << ": " << ec.message() << std::endl;
        } else {
            // Port opened successfully, now we need a way to check VID/PID.
            // Boost.Asio does not provide a direct way to get VID/PID.
            // This would still require platform-specific code.
            // For the purpose of this example, we'll assume the first port found is the correct one.
            std::cout << "Opened " << port_name << ", but cannot verify VID/PID with Boost.Asio alone." << std::endl;
            port.close();
        }
    }

    return "";
}

int main() {
    std::string port = find_serial_port();
    if (!port.empty()) {
        std::cout << "Found serial port: " << port << std::endl;
    } else {
        std::cout << "Serial port not found." << std::endl;
    }
    return 0;
}
