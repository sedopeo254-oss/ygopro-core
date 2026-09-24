#include "card.h"
#include "duel.h"
#include "effect.h"
#include "field.h"
#include "group.h"
#include "ocgapi.h"

#include <cstdlib>
#include <iostream>

namespace {
void expect(bool condition, const char* message) {
	if(condition)
		return;
	std::cerr << "FAILED: " << message << '\n';
	std::exit(1);
}

void read_card(void*, uint32_t code, OCG_CardData* data) {
	*data = {};
	data->code = code;
	data->type = TYPE_MONSTER;
}
void read_card_done(void*, OCG_CardData*) {}
int read_script(void*, OCG_Duel, const char*) { return 0; }
void log_message(void*, const char*, int) {}

void add_private_card(duel& game, uint8_t side, uint8_t duelist,
		uint32_t code, uint8_t location) {
	const OCG_NewCardInfo info{
		side,
		duelist,
		code,
		side,
		location,
		0,
		POS_FACEDOWN_DEFENSE
	};
	OCG_DuelNewCard(&game, &info);
}
}

int main() {
	OCG_DuelOptions options{};
	options.seed[0] = 1;
	options.flags = DUEL_2_V_1;
	options.team1 = { 4000, 5, 1 };
	options.team2 = { 4000, 5, 1 };
	options.cardReader = read_card;
	options.cardReaderDone = read_card_done;
	options.scriptReader = read_script;
	options.logHandler = log_message;

	bool valid_lua = true;
	duel game(options, valid_lua);
	expect(valid_lua, "Lua runtime must initialize");
	auto& field = *game.game_field;

	expect(field.multiplayer.mode() == MultiplayerMode::TWO_V_ONE,
		"the clean flag must create the dedicated TWO_V_ONE mode");
	expect(field.player[0].list_mzone.size() == 14
			&& field.player[0].list_szone.size() == 16,
		"P1 and P2 must own two independent allied fields");
	expect(field.player[1].list_mzone.size() == 7
			&& field.player[1].list_szone.size() == 8,
		"P3 must keep one standard field");

	// Create P2's private logical piles without touching P1's active piles.
	add_private_card(game, 0, 1, 1101, LOCATION_DECK);
	add_private_card(game, 0, 1, 1102, LOCATION_EXTRA);
	expect(field.get_logical_list(0, LOCATION_DECK, 1).size() == 1,
		"P2 must have an independent Deck");
	expect(field.get_logical_list(0, LOCATION_EXTRA, 1).size() == 1,
		"P2 must have an independent Extra Deck");

	auto* p1_monster = game.new_card(2001);
	p1_monster->owner = 0;
	p1_monster->owner_duelist = 0;
	field.add_card(0, p1_monster, LOCATION_MZONE, 0, false, 0);
	expect(p1_monster->current.sequence == 0
			&& p1_monster->current.duelist == 0,
		"P1 monster must stay on P1's encoded field");

	expect(field.tag_swap_to(0, 1), "P2 private resources must become active");
	auto* p2_monster = game.new_card(2002);
	p2_monster->owner = 0;
	p2_monster->owner_duelist = 1;
	field.add_card(0, p2_monster, LOCATION_MZONE, 0, false, 1);
	expect(p2_monster->current.sequence == 7
			&& p2_monster->current.duelist == 1,
		"P2 monster must stay on P2's encoded field");
	expect(field.player[0].list_mzone[0] == p1_monster
			&& field.player[0].list_mzone[7] == p2_monster,
		"P1 and P2 fields must remain simultaneously present");

	auto* p3_monster = game.new_card(2003);
	p3_monster->owner = 1;
	p3_monster->owner_duelist = 0;
	field.add_card(1, p3_monster, LOCATION_MZONE, 0, false, 0);
	expect(field.player[1].list_mzone[0] == p3_monster,
		"P3 must have its own standard monster field");

	field.get_logical_lp(0, 0) = 3500;
	field.get_logical_lp(0, 1) = 2700;
	field.get_logical_lp(1, 0) = 6100;
	expect(field.get_logical_lp(0, 0) == 3500
			&& field.get_logical_lp(0, 1) == 2700
			&& field.get_logical_lp(1, 0) == 6100,
		"all three players must keep independent Life Points");

	// Fusion/field resources are intentionally shared by the allied side.
	card_set fusion_material;
	field.get_fusion_material(0, &fusion_material);
	expect(fusion_material.count(p1_monster) == 1
			&& fusion_material.count(p2_monster) == 1,
		"an allied Fusion must be able to use monsters from both allied fields");

	// Public GY/Banish are shared resources; private Hand/Deck/Extra are not.
	auto* p2_grave = game.new_card(3001);
	p2_grave->owner = 0;
	p2_grave->owner_duelist = 1;
	field.add_card(0, p2_grave, LOCATION_GRAVE, 0, false, 1);
	auto* p2_banish = game.new_card(3002);
	p2_banish->owner = 0;
	p2_banish->owner_duelist = 1;
	field.add_card(0, p2_banish, LOCATION_REMOVED, 0, false, 1);
	expect(field.tag_swap_to(0, 0), "P1 resources must become active again");
	auto* p1_grave = game.new_card(3003);
	p1_grave->owner = 0;
	p1_grave->owner_duelist = 0;
	field.add_card(0, p1_grave, LOCATION_GRAVE, 0, false, 0);

	auto public_group = game.new_group();
	field.filter_field_card(0, LOCATION_GRAVE | LOCATION_REMOVED, 0, public_group);
	expect(public_group->container.count(p1_grave) == 1
			&& public_group->container.count(p2_grave) == 1
			&& public_group->container.count(p2_banish) == 1,
		"P1 effects must be able to use P2 public GY/Banish resources");
	expect(field.filter_field_card(0, LOCATION_DECK, 0, nullptr)
			== static_cast<int32_t>(field.get_logical_list(0, LOCATION_DECK, 0).size()),
		"P2 Deck must never be merged into P1's private Deck query");

	// Deck Masters (or any private logical monster) must enter their owner's
	// field even while the teammate is the active/focused duelist.
	auto* p2_deck_master = game.new_card(153000012);
	p2_deck_master->owner = 0;
	p2_deck_master->owner_duelist = 1;
	field.add_card(0, p2_deck_master, LOCATION_DECK, 0, false, 1);
	expect(field.player[0].current_duelist == 0,
		"Deck Master owner test must begin while P1 is active");
	expect(field.move_card(0, p2_deck_master, LOCATION_MZONE, 1),
		"P2 Deck Master must be movable to the field");
	expect(p2_deck_master->current.duelist == 1
			&& p2_deck_master->current.sequence == 8
			&& field.player[0].list_mzone[8] == p2_deck_master,
		"P2 Deck Master must summon to P2's own field, never P1's");
	expect(field.move_card(0, p2_deck_master, LOCATION_GRAVE, 0),
		"P2 Deck Master must be movable to its Graveyard");
	expect(field.get_logical_list(0, LOCATION_GRAVE, 1).back() == p2_deck_master,
		"P2 Deck Master must return to P2's own Graveyard");

	std::cout << "All clean 2 vs 1 field tests passed.\n";
	return 0;
}
