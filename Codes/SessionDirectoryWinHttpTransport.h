#pragma once

#include "SessionDirectoryBrokerTransport.h"

namespace ToolKit::ToolKitNetworking {
SessionDirectoryBrokerTransportPtr CreateWinHttpSessionDirectoryBrokerTransport(
    const SessionDirectoryBrokerRuntimeConfig &config, String &detailMessage);
} // namespace ToolKit::ToolKitNetworking
