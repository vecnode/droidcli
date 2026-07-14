#include "ai/types.hpp"

namespace droidcli::ai {

core::String chat_role_to_string(const ChatRole role)
{
	switch (role)
	{
	case ChatRole::System:
		return "system";
	case ChatRole::User:
		return "user";
	case ChatRole::Assistant:
		return "assistant";
	case ChatRole::Tool:
		return "tool";
	default:
		return "user";
	}
}

ChatRole chat_role_from_string(const core::String& role)
{
	if (role == "system")
	{
		return ChatRole::System;
	}
	if (role == "assistant")
	{
		return ChatRole::Assistant;
	}
	if (role == "tool")
	{
		return ChatRole::Tool;
	}
	return ChatRole::User;
}

} // namespace droidcli::ai
