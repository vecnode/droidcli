#include "core/spawn.hpp"

#include <cassert>
#include <iostream>
#include <mutex>
#include <vector>

int main()
{
	using namespace droidcli::core;

	// Basic: fn runs on the spawned thread, joins cleanly, no sink required.
	{
		bool ran = false;
		std::thread t = spawn("test.basic", [&ran]() { ran = true; });
		t.join();
		assert(ran);
	}

	// Sink observes "spawned" immediately (before the thread necessarily
	// starts running fn) and "joined" once fn returns normally.
	{
		std::mutex events_mutex;
		std::vector<std::string> events;
		auto sink = [&](const String& name, const String& event)
		{
			std::lock_guard<std::mutex> lock(events_mutex);
			assert(name == "test.sink");
			events.push_back(event);
		};

		std::thread t = spawn("test.sink", []() {}, sink);
		t.join();

		std::lock_guard<std::mutex> lock(events_mutex);
		assert(events.size() == 2);
		assert(events[0] == "spawned");
		assert(events[1] == "joined");
	}

	// NOTE on what isn't tested here: spawn()'s "threw: <what>" sink report
	// happens on the way to rethrowing - matching a bare std::thread's
	// std::terminate()-on-uncaught-exception behavior exactly, not
	// replacing it. Actually letting fn's exception escape spawn()'s
	// wrapper would terminate this test process by design, so that path
	// isn't (and can't safely be) exercised by an in-process unit test.
	// cli/tui.cpp's two real callers both catch inside fn already (see
	// PolledState/ChatWork's try/catch blocks), which is the documented
	// pattern - "threw" is a last-resort signal for a caller that didn't.

	std::cout << "spawn_test passed" << std::endl;
	return 0;
}
