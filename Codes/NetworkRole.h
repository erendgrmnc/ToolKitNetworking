#pragma once

namespace ToolKit::ToolKitNetworking {
// Legacy runtime/editor role. Kept for compatibility while session-facing
// configuration is introduced on top of the existing component.
enum class NetworkRole { None, Client, DedicatedServer, Host };
} // namespace ToolKit::ToolKitNetworking
