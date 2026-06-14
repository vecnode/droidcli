#include "initialize.hpp"

#include "particle/forming_solver.hpp"
#include "particle/transition_graph.hpp"

namespace metaagent {

void initialize_defaults()
{
	particle::TransitionGraph::register_defaults();
	particle::FormingSolverRegistry::register_defaults();
}

} // namespace metaagent
