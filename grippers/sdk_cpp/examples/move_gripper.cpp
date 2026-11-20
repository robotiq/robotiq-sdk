// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! Runtime-mode example — the way to control a gripper with this SDK.
//! The gripper's fingers move (activation sweep, open, close): keep the
//! jaws clear.
//! Usage: move_gripper <port> [baudrate] [model]

#include <cstdlib> // EXIT_SUCCESS / EXIT_FAILURE, main()'s return codes below — unrelated to Gripper itself
#include <memory> // smart pointers: std::unique_ptr to own the Gripper, std::shared_ptr for the Logger it takes
#include <optional> // std::optional from profileNamed() and the unit-conversion functions below
#include <iostream> // std::cerr for usage errors
#include <string> // std::stoul (baudrate parsing)

#include <Robotiq/gripper.hpp> // Gripper, GripperCommand/Status
#include <Robotiq/gripper/device_profile.hpp> // DeviceProfile and the shipped profiles
#include <Robotiq/gripper/stderr_logger.hpp>
#include <Robotiq/gripper/units.hpp>

#include "gripper_events.hpp" // connectGripper(), activateOrRecover(), moveTo(): shared with the other examples

// The handful of SDK types this example touches directly:
using Robotiq::DeviceProfile;
using Robotiq::Gripper;
using Robotiq::GripperCommand;

namespace {
// Any rate a serial link plausibly runs at. Also what catches a negative:
// stoul("-1") wraps to a huge value rather than throwing.
//! [baudrate-bounds]
constexpr unsigned long kMinBaudrate = 1;
constexpr unsigned long kMaxBaudrate = 1000000;
//! [baudrate-bounds]

constexpr double kEffort = 1.0; // maximum grip effort
constexpr const char* kDefaultModel = "2f_85";
static_assert(Robotiq::profiles::profileNamed(kDefaultModel).has_value());

// Shared by the argument checks below.
void printUsage(const char* program)
{
   std::cerr << "Usage: " << program << " <port> [baudrate] [model]\n"
             << "  model:";
   for(const Robotiq::profiles::NamedProfile& entry : Robotiq::profiles::kNamed)
   {
      std::cerr << ' ' << entry.name << (entry.name == kDefaultModel ? " (default)" : "");
   }
   std::cerr << '\n';
}
} // namespace

//! \brief Connect, activate, open, then close — see the file header comment.
//! \param argc Argument count; expects 2 to 4 (program name, port, [baudrate], [model]).
//! \param argv argv[1] is the serial port; argv[2], if given, is the baudrate;
//!             argv[3], if given, names the gripper model's profile.
//! \return EXIT_SUCCESS on a full connect/activate/open/close cycle;
//!         EXIT_FAILURE on a bad argument, a connection error, or a timeout.
int main(int argc, char* argv[])
{
   //! [argument-handling]
   if(argc < 2 || argc > 4)
   {
      printUsage(argv[0]);
      return EXIT_FAILURE;
   }
   //! [argument-handling]

   Robotiq::ConnectionConfig config;
   config.serial.port = argv[1];
   if(argc > 2)
   {
      try
      {
         //! [baudrate-parse-and-check]
         const unsigned long parsed = std::stoul(argv[2]);
         if(parsed < kMinBaudrate || parsed > kMaxBaudrate)
         {
            throw std::out_of_range("baudrate outside the supported range");
         }
         //! [baudrate-parse-and-check]
         config.serial.baudrate = static_cast<uint32_t>(parsed);
      }
      catch(const std::exception&)
      {
         std::cerr << "Invalid baudrate '" << argv[2] << "'\n";
         return EXIT_FAILURE;
      }
   }

   //! [model-selection]
   const char* const model = argc > 3 ? argv[3] : kDefaultModel;
   const std::optional<DeviceProfile> named = Robotiq::profiles::profileNamed(model);
   if(!named)
   {
      std::cerr << "Unknown model '" << model << "'\n";
      printUsage(argv[0]);
      return EXIT_FAILURE;
   }
   const DeviceProfile profile = *named;
   //! [model-selection]

   // The SDK logs through an injectable sink; naming the instances
   // tells library and application lines apart in the shared stream.
   // (A real integration gets the same separation from its injected
   // adapter, e.g. a named rclcpp logger.)
   //! [logger-example-name]
   auto logger = std::make_shared<Robotiq::StderrLogger>("example");
   //! [logger-example-name]

   const std::unique_ptr<Gripper> gripper = examples::connectGripper(config);
   if(!gripper)
   {
      return EXIT_FAILURE;
   }

   logger->log(Robotiq::Logger::Level::Info, "Activating...");
   if(!examples::activateOrRecover(*gripper, *logger))
   {
      return EXIT_FAILURE;
   }

   // Keep one command block and update it before each send: it is
   // persistent state, not rebuilt per move.
   GripperCommand command = GripperCommand::defaults(); // GoTo added by moveTo
   //! [speed-optional-check]
   const std::optional<uint8_t> speed = Robotiq::units::speedToRegister(profile.maxSpeed, profile);
   const std::optional<uint8_t> force = Robotiq::units::effortToRegister(kEffort);
   if(!speed || !force)
   {
      logger->log(Robotiq::Logger::Level::Error, "the requested speed or effort has no register value");
      return EXIT_FAILURE;
   }
   command.speed = *speed;
   command.force = *force;
   //! [speed-optional-check]

   logger->log(Robotiq::Logger::Level::Info, "Opening...");
   if(!examples::moveTo(*gripper, command, profile.maxOpening, profile, *logger))
   {
      return EXIT_FAILURE;
   }

   logger->log(Robotiq::Logger::Level::Info, "Closing...");
   if(!examples::moveTo(*gripper, command, profile.minOpening, profile, *logger))
   {
      return EXIT_FAILURE;
   }
   return EXIT_SUCCESS;
}
