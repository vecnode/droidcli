#include "core/spawn.hpp"

#include <exception>

namespace droidcli::core {

std::thread spawn(const core::String& thread_name, std::function<void()> fn, ThreadEventSink sink)
{
	if (sink)
	{
		sink(thread_name, "spawned");
	}

	return std::thread([thread_name, fn = std::move(fn), sink]()
	{
		try
		{
			fn();
			if (sink)
			{
				sink(thread_name, "joined");
			}
		}
		catch (const std::exception& e)
		{
			if (sink)
			{
				sink(thread_name, core::String("threw: ") + e.what());
			}
			throw;
		}
		catch (...)
		{
			if (sink)
			{
				sink(thread_name, "threw: unknown exception");
			}
			throw;
		}
	});
}

} // namespace droidcli::core
