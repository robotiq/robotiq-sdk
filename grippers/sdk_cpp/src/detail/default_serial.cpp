// Copyright (c) 2023 PickNik, Inc.
// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <Robotiq/detail/default_serial.hpp>

#include <memory>
#include <string>
#include <utility>

#include <Robotiq/common/latency_timer.hpp>
#include <Robotiq/common/serial_error.hpp>
#include <Robotiq/common/serial_port.hpp>
#include <Robotiq/detail/default_logger.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/serial_io_exception.hpp>

namespace Robotiq::detail {
namespace {
// The shared SerialPort reports failure as common::SerialError, which is
// internal; the Serial contract promises SerialIOException.
template <typename Operation>
decltype(auto) translateErrors(Operation&& operation)
{
   try
   {
      return operation();
   }
   catch(const common::SerialError& error)
   {
      throw SerialIOException(error.what());
   }
}
} // namespace

DefaultSerial::DefaultSerial(SerialConfig config, std::shared_ptr<Logger> logger)
   : _config(std::move(config))
   , _logger(logger ? std::move(logger) : makeDefaultLogger())
{
}

DefaultSerial::~DefaultSerial()
{
   DefaultSerial::close();
}

void DefaultSerial::open()
{
   if(_port != nullptr)
   {
      _logger->log(Logger::Level::Debug,
                   "open() called on " + _config.port + " but the port is already open; ignoring.");
      return;
   }
   if(_config.port.empty())
   {
      throw SerialIOException("Cannot open serial port: port path is empty");
   }
   _logger->log(Logger::Level::Debug,
                "opening serial port '" + _config.port + "' at " + std::to_string(_config.baudrate) + " baud");

   // Published only once configured: a throw here leaves the connection
   // closed rather than half-open.
   auto port = std::make_unique<common::SerialPort>();
   translateErrors([&] { port->open(_config.port, _config.baudrate); });
   _port = std::move(port);

#ifdef __linux__
   if(_config.latencyTimerMs > 0)
   {
      const std::string timerPath = common::latencyTimerPath(_config.port);
      if(timerPath.empty())
      {
         _logger->log(Logger::Level::Debug, _config.port + " is not an FTDI adapter; no latency_timer to set.");
      }
      else if(common::applyLatencyTimer(timerPath, _config.latencyTimerMs))
      {
         _logger->log(Logger::Level::Info,
                      "FTDI latency_timer set to " + std::to_string(_config.latencyTimerMs) + " ms on " + _config.port
                         + ".");
      }
      else
      {
         _logger->log(Logger::Level::Warn,
                      "Could not set FTDI latency_timer to " + std::to_string(_config.latencyTimerMs) + " ms on "
                         + _config.port
                         + ". Modbus cycle latency may be ~3x higher than expected (the kernel default "
                           "is 16 ms). Either run with permissions to write "
                           "/sys/bus/usb-serial/devices/<dev>/latency_timer, or ship a udev rule that "
                           "sets it at plug time.");
      }
   }
#elif defined(__APPLE__)
   if(_config.latencyTimerMs > 0)
   {
      _logger->log(Logger::Level::Warn,
                   "FTDI latency timer left at the macOS default (~16 ms) on " + _config.port
                      + "; the exchange rate is capped near 60 Hz. macOS has no API to set it from here — "
                        "configure the FTDI driver instead (see the README's macOS serial-port notes).");
   }
#endif
}

bool DefaultSerial::isOpen() const
{
   return _port != nullptr;
}

void DefaultSerial::close()
{
   _port.reset();
}

std::vector<uint8_t> DefaultSerial::read(size_t size, std::chrono::milliseconds timeout)
{
   if(_port == nullptr)
   {
      throw SerialIOException("read called on closed port");
   }

   std::vector<uint8_t> data(size);
   data.resize(translateErrors([&] { return _port->read(data.data(), size, timeout); }));
   return data;
}

void DefaultSerial::write(const std::vector<uint8_t>& data)
{
   if(_port == nullptr)
   {
      throw SerialIOException("write called on closed port");
   }

   const size_t written = translateErrors([&] { return _port->write(data.data(), data.size(), _config.timeout); });
   if(written < data.size())
   {
      throw SerialIOException("write timeout: wrote " + std::to_string(written) + " of " + std::to_string(data.size())
                              + " bytes");
   }
   if(!translateErrors([&] { return _port->drain(_config.timeout); }))
   {
      throw SerialIOException("write timeout: the bytes written did not drain");
   }
}

std::chrono::milliseconds DefaultSerial::getTimeout() const
{
   return _config.timeout;
}

const SerialConfig& DefaultSerial::getConfig() const
{
   return _config;
}
} // namespace Robotiq::detail
