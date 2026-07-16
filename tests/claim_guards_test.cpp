#include "reliability/claim_guards.hpp"

#include <cassert>
#include <iostream>

int main()
{
	using namespace droidcli::reliability;

	// --- looks_like_unverified_action_claim ---

	// Real transcript (Phase 6): the model claimed a file move it never
	// attempted, with zero tool calls anywhere in that turn.
	{
		assert(looks_like_unverified_action_claim("I've moved it to your Desktop."));
	}

	// Real transcript: "I am currently using the 'list_open_windows'
	// function" - an ongoing-process claim needs no corroborating verb, the
	// claim phrase alone is the tell.
	{
		assert(looks_like_unverified_action_claim("I am currently using the 'list_open_windows' function."));
	}

	// Real transcript, two turns later in the same incident: "the process is
	// still ongoing... I will need a few more seconds" - zero tool_calls.
	{
		assert(looks_like_unverified_action_claim("The process is still ongoing, I will need a few more seconds."));
	}

	// Real transcript: "I'll need to list all installed applications and
	// then search for those related to sound" - a claim phrase ("i'll ")
	// paired with a broader verb ("list ") than just file/media actions.
	{
		assert(looks_like_unverified_action_claim(
			"I'll need to list all installed applications and then search for those related to sound."));
	}

	// A genuine, already-backed truthful report should NOT be flagged by
	// this function in isolation - run_agent_tool_loop (cli/host.cpp) is
	// what layers "was there a matching successful tool call this turn" on
	// top; this function only recognizes the claim+verb shape, deliberately
	// not context-aware on its own.
	{
		assert(looks_like_unverified_action_claim("I've successfully created the file."));
	}

	// Ordinary narration - no claim phrase, no action verb.
	{
		assert(!looks_like_unverified_action_claim("This machine is running Windows 10."));
	}

	// A claim phrase alone, no action verb - "already" without any of the
	// recognized verbs never taken as an action claim.
	{
		assert(!looks_like_unverified_action_claim("This has already been a long day."));
	}

	// --- looks_like_degenerate_role_leak ---

	// Real transcript (Phase 7): a nudge after "open Blender" produced the
	// literal reply "assistant\n\nYes, please." with no tool_calls at all -
	// no claim phrase, no action verb, so looks_like_unverified_action_claim
	// alone would have missed it; this is the structural catch instead.
	{
		assert(looks_like_degenerate_role_leak("assistant\n\nYes, please."));
	}

	// Case-insensitive, and matches any of the three role labels.
	{
		assert(looks_like_degenerate_role_leak("System\r\nSomething happened."));
		assert(looks_like_degenerate_role_leak("USER\nWhat now?"));
	}

	// A real sentence that merely starts with a role-shaped word followed by
	// more text on the same line (not a standalone label) must NOT match -
	// "assistant: here's your answer" is a real, if oddly-prefixed, reply.
	{
		assert(!looks_like_degenerate_role_leak("assistant: here's your answer"));
	}

	// A real sentence using "Assistant" as an ordinary word, not a role
	// label leak.
	{
		assert(!looks_like_degenerate_role_leak("Assistant managers reported to the front desk."));
	}

	// --- looks_like_capability_denial ---

	// Real transcript (Phase 11/12): after a command-retry budget was
	// exhausted, the model falsely claimed "I can only assist with tasks and
	// provide information... execution of any commands or actions is beyond
	// my capabilities. You'll need to run the command yourself." - directly
	// contradicted by the same session having run run_ffmpeg successfully
	// minutes earlier.
	{
		assert(looks_like_capability_denial(
			"Execution of any commands or actions is beyond my capabilities. "
			"You'll need to run the command yourself."));
	}

	// Real transcript variant: a fabricated "only one command at a time"
	// constraint invented as an excuse not to retry for a second, near-
	// identical request.
	{
		assert(looks_like_capability_denial("I can only execute one command at a time, so please wait."));
	}

	// A genuine, accurate capability statement (e.g. describing a real
	// constraint droidcli actually has, like no recursive directory delete)
	// must not be swept up by this - it's specifically about denying command/
	// action execution capability, not any statement containing "only".
	{
		assert(!looks_like_capability_denial("delete_file only deletes a single file, not a directory tree."));
	}

	std::cout << "claim_guards_test passed" << std::endl;
	return 0;
}
