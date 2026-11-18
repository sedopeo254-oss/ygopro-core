#include "../anime_zone_rules.h"
#include <cstdlib>
#include <iostream>
using namespace anime_zone_rules;
static void check(bool ok,const char* msg) {
	if(!ok){std::cerr<<"FAIL: "<<msg<<"\n";std::exit(1);}
}
int main(){
	Policy seal;
	seal.features=REAR_MONSTER|FRONT_GUARDS_REAR|CROSS_ROW_MOVEMENT
		|CROSS_ROW_SUMMON|MODIFY_MONSTER_STATS;
	seal.attack_bonus=500;
	check(seal.valid()&&seal.has(REAR_MONSTER),"native feature policy");
	check(!seal.has(CUSTOM_ANIME_EFFECT),"capability opt-in only");
	check(physical_row(0x04)==Row::FRONT_MONSTER,"physical MZONE");
	check(physical_row(0x08)==Row::REAR_SPELL_TRAP,"physical SZONE");
	check(physical_row(0x10)==Row::OUTSIDE_FIELD,"Graveyard is not row");
	check(counts_as_monster(Row::FRONT_MONSTER,true,false,false),"front remains playable");
	check(counts_as_monster(Row::REAR_SPELL_TRAP,true,true,true),"rear monster recognized");
	check(!counts_as_monster(Row::REAR_SPELL_TRAP,false,true,true),"nonmonster stays spell");
	check(!counts_as_monster(Row::REAR_SPELL_TRAP,true,true,false),"capability must be active");
	check(can_declare_attack(Row::REAR_SPELL_TRAP,true,true),"rear attacks with opt-in");
	check(!can_declare_attack(Row::REAR_SPELL_TRAP,true,false),"no implicit rear attacks");
	check(!rear_is_attackable(true,1,true),"front protects rear");
	check(rear_is_attackable(true,0,true),"back row exposed without front");
	check(rear_is_attackable(true,3,false),"protection is optional for future mechanics");
	check(can_place_in_rear(4,true,true,seal),"fifth rear slot available");
	check(!can_place_in_rear(5,true,true,seal),"no sixth slot");
	check(!can_place_in_rear(0,false,true,seal),"cannot overwrite S/T card");
	check(!can_place_in_rear(0,true,false,seal),"marker authorization required");
	check(physical_location_from_virtual(0x2000)==0x04,"virtual front");
	check(physical_location_from_virtual(0x4000)==0x08,"virtual back");
	check(physical_location_from_virtual(0x2001)==0,"reject mixed location bitmask");
	Policy futuristic=seal;
	futuristic.features |= CUSTOM_ANIME_EFFECT;
	check(futuristic.has(CUSTOM_ANIME_EFFECT),"extensible novel effects");
	std::cout<<"Independent anime zone policy engine: PASS\n";
}
