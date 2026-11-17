// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// Links against the installed package, common/ modules included. Built,
// never run.

#include <Robotiq/gripper.hpp>

int main(int argc, char* argv[])
{
   if(argc < 2)
   {
      return 0;
   }
   Robotiq::ConnectionConfig config;
   config.serial.port = argv[1];
   Robotiq::Gripper gripper(config);
   return 0;
}
