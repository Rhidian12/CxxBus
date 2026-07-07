#pragma once

#include "DBus.h"

class DBusReply
{
private:

public:
  DBusReply();
  DBusReply(std::vector<byte> const & data);
};