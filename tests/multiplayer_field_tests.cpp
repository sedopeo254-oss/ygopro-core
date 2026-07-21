#include "card.h"
#include "duel.h"
#include "field.h"
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

void read_card_done(void*, OCG_CardData*) {
}

int read_script(void*, OCG_Duel, const char*) {
	return 0;
}

void log_message(void*, const char*, int) {
}

void initialize_extra_duelist(duel& game, uint8_t duelist_index, uint32_t code) {
	const OCG_NewCardInfo info{
		0,
		duelist_index,
		code,
		0,
		LOCATION_DECK,
		0,
		POS_FACEDOWN_DEFENSE
	};
	OCG_DuelNewCard(&game, &info);
}
}

int main() {
	OCG_DuelOptions options{};
	options.seed[0] = 1;
	options.flags = DUEL_3_V_1;
	options.team1 = { 4000, 5, 1 };
	options.team2 = { 4000, 5, 1 };
	options.cardReader = read_card;
	options.cardReaderDone = read_card_done;
	options.scriptReader = read_script;
	options.logHandler = log_message;

	bool valid_lua = true;
	duel game(options, valid_lua);
	expect(valid_lua, "the embedded Lua runtime must initialize");
	auto& field = *game.game_field;
	expect(field.player[0].list_mzone.size() == 21, "the allied monster fields must expose 21 internal slots");
	expect(field.player[0].list_szone.size() == 24, "the allied spell/trap fields must expose 24 internal slots");

	initialize_extra_duelist(game, 1, 1001);
	initialize_extra_duelist(game, 2, 1002);

	auto* serenity = game.new_card(2000);
	serenity->owner = 0;
	serenity->owner_duelist = 0;
	field.add_card(0, serenity, LOCATION_MZONE, 0);
	expect(serenity->current.sequence == 0, "Serenity's first monster zone must map to slot 0");

	expect(field.tag_swap_to(0, 1), "the active allied resources must switch to Tristan");
	auto* tristan = game.new_card(2001);
	tristan->owner = 0;
	tristan->owner_duelist = 1;
	field.add_card(0, tristan, LOCATION_MZONE, 0);
	expect(tristan->current.sequence == 7, "Tristan's first monster zone must map to slot 7");

	expect(field.tag_swap_to(0, 2), "the active allied resources must switch to Duke");
	auto* duke = game.new_card(2002);
	duke->owner = 0;
	duke->owner_duelist = 2;
	field.add_card(0, duke, LOCATION_MZONE, 0);
	expect(duke->current.sequence == 14, "Duke's first monster zone must map to slot 14");
	expect(field.player[0].list_mzone[0] == serenity
		&& field.player[0].list_mzone[7] == tristan
		&& field.player[0].list_mzone[14] == duke,
		"all three allied fields must remain simultaneously present");

	auto* tristan_hand = game.new_card(2003);
	tristan_hand->owner = 0;
	tristan_hand->owner_duelist = 1;
	field.add_card(0, tristan_hand, LOCATION_HAND, 0);
	auto* tristan_extra = game.new_card(2004);
	tristan_extra->data.type = TYPE_MONSTER | TYPE_FUSION;
	tristan_extra->owner = 0;
	tristan_extra->owner_duelist = 1;
	field.add_card(0, tristan_extra, LOCATION_EXTRA, 0);
	auto* tristan_banished = game.new_card(2005);
	tristan_banished->owner = 0;
	tristan_banished->owner_duelist = 1;
	field.add_card(0, tristan_banished, LOCATION_REMOVED, 0);
	expect(field.get_logical_list(0, LOCATION_DECK, 1).size() == 1
		&& field.get_logical_list(0, LOCATION_HAND, 1).size() == 1
		&& field.get_logical_list(0, LOCATION_EXTRA, 1).size() == 1
		&& field.get_logical_list(0, LOCATION_REMOVED, 1).size() == 1,
		"Tristan's deck, hand, extra deck and banished pile must stay independent while Duke is active");

	expect(field.move_card(0, tristan, LOCATION_GRAVE, 0), "Tristan's card must move to its owner's graveyard");
	expect(field.player[0].list_grave.empty(), "Duke's active graveyard must stay separate");
	expect(field.tag_swap_to(0, 1), "the active resources must switch back to Tristan");
	expect(field.player[0].list_grave.size() == 1 && field.player[0].list_grave.front() == tristan,
		"Tristan's graveyard must be restored with his card");

	field.get_logical_lp(0, 0) = 3100;
	field.get_logical_lp(0, 1) = 2200;
	field.get_logical_lp(0, 2) = 1300;
	expect(field.get_logical_lp(0, 0) == 3100
		&& field.get_logical_lp(0, 1) == 2200
		&& field.get_logical_lp(0, 2) == 1300,
		"each allied duelist must retain independent life points");

	auto* nezbitt = game.new_card(3000);
	nezbitt->owner = 1;
	field.add_card(1, nezbitt, LOCATION_MZONE, 0);
	field.core.attacker = nezbitt;
	field.core.attack_target_duelist = 0;
	card_vector attack_targets;
	field.get_attack_target(nezbitt, &attack_targets);
	expect(attack_targets.size() == 1 && attack_targets.front() == serenity,
		"Nezbitt must only see Serenity's monsters after selecting her as the attack target");
	field.core.attack_target_duelist = 2;
	attack_targets.clear();
	field.get_attack_target(nezbitt, &attack_targets);
	expect(attack_targets.size() == 1 && attack_targets.front() == duke,
		"changing the selected attack target must project Duke's monster field");
	field.core.subunits.clear();
	field.core.attack_target = nullptr;
	field.damage(nullptr, REASON_BATTLE, 1, nezbitt, 0, 500);
	auto* direct_damage = Processors::get_opt_variant<Processors::Damage>(field.core.subunits.back());
	expect(direct_damage && direct_damage->duelist == 2,
		"a direct attack must queue damage for the selected allied duelist only");

	std::cout << "All multiplayer field tests passed.\n";
	return 0;
}
