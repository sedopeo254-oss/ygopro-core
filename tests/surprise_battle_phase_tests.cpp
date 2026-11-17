#include "surprise_battle_phase.h"
#include <cstdlib>
#include <iostream>
using surprise_battle_phase::State;
static void check(bool ok,const char* message) {
	if(!ok) {std::cerr<<"FAILED: "<<message<<'\n'; std::exit(1);}
}
int main() {
	State state;
	check(!state.can_request(0,0,4,1,false),"not from Main Phase");
	check(!state.can_request(0,1,0x200,1,false),"not opponent's EP");
	check(!state.can_request(0,0,0x200,0,false),"cannot repeat a BP that never occurred");
	check(!state.can_request(0,0,0x200,1,true),"forced turn end");
	check(state.request(0,0,0x200,1,false),"can request within own EP");
	check(state.pending(),"request remains pending until EP fully resolves");
	check(!state.request(0,0,0x200,1,false),"only once per turn");
	check(state.enter_extra_battle(),"only one extra BP begins");
	check(state.in_extra_battle() && state.used_this_turn(),"same-turn state");
	check(!state.enter_extra_battle(),"no infinite repeated BP");
	check(!state.request(0,0,0x200,2,false),"cannot request from extra BP");
	check(state.finish_extra_battle(),"return from BP to same-turn EP");
	check(!state.finish_extra_battle(),"only one return");
	check(!state.request(0,0,0x200,2,false),"EP cannot schedule second extra BP");
	state.reset();
	check(state.can_request(1,1,0x200,1,false),"next turn is independent");
	std::cout<<"Same-turn extra Battle Phase: PASS\n";
}
