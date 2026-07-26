#include "card.h"
#include "duel.h"
#include "effect.h"
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
	auto* duke_effect = game.new_effect();
	duke_effect->owner = duke;
	duke_effect->handler = duke;
	field.core.reason_effect = duke_effect;
	expect(field.get_effect_duelist(0) == 2 && field.get_response_player(0) == 4,
		"a Duke Deck Master ability must route its prompts to Duke even outside his turn");
	field.core.reason_effect = nullptr;
	expect(field.get_response_player(0) == 4,
		"the current allied field must remain the response fallback without an effect handler");

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

	const auto tristan_hand_count = field.get_logical_list(0, LOCATION_HAND, 1).size();
	const auto duke_hand_count = field.get_logical_list(0, LOCATION_HAND, 2).size();
	Processors::Draw duke_draw(0, nullptr, REASON_EFFECT, 0, 0, 1, 2);
	expect(!field.process(duke_draw), "drawing for an inactive allied duelist must complete its first processing step");
	duke_draw.step = 1;
	expect(field.process(duke_draw), "drawing for an inactive allied duelist must complete");
	expect(field.get_logical_list(0, LOCATION_DECK, 2).empty()
		&& field.get_logical_list(0, LOCATION_HAND, 2).size() == duke_hand_count + 1,
		"the drawn card must move from Duke's deck to Duke's hand");
	expect(field.get_logical_list(0, LOCATION_HAND, 1).size() == tristan_hand_count,
		"drawing for Duke must not alter Tristan's currently visible hand");
	Processors::Draw serenity_overdraw(0, nullptr, REASON_EFFECT, 0, 0, 1, 0);
	expect(!field.process(serenity_overdraw), "Serenity's failed draw must be processed");
	Processors::Draw duke_overdraw(0, nullptr, REASON_EFFECT, 0, 0, 1, 2);
	expect(!field.process(duke_overdraw), "Duke's failed draw must be processed");
	expect((field.core.multiplayer_overdraw_mask & 0x05) == 0x05,
		"failed draws for multiple allied duelists must be tracked independently");
	field.core.multiplayer_overdraw_mask = 0;
	field.core.overdraw[0] = false;

	field.get_logical_lp(0, 0) = 3100;
	field.get_logical_lp(0, 1) = 2200;
	field.get_logical_lp(0, 2) = 1300;
	expect(field.get_logical_lp(0, 0) == 3100
		&& field.get_logical_lp(0, 1) == 2200
		&& field.get_logical_lp(0, 2) == 1300,
		"each allied duelist must retain independent life points");
	Processors::Recover duke_recover(0, nullptr, REASON_EFFECT, 0, 0, 200, false, 2);
	expect(!field.process(duke_recover), "recovering an inactive allied duelist must complete its first step");
	duke_recover.step = 1;
	expect(!field.process(duke_recover), "recovering an inactive allied duelist must apply to life points");
	expect(field.get_logical_lp(0, 2) == 1500
		&& field.get_logical_lp(0, 0) == 3100
		&& field.get_logical_lp(0, 1) == 2200,
		"LP recovery must affect only the selected logical player");

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
	field.core.subunits.clear();
	Processors::Damage effect_damage(0, nullptr, REASON_EFFECT, 1, nezbitt, 0, 700, false, 0, true);
	expect(!field.process(effect_damage), "effect damage must pause for teammate interception");
	auto* intercept_prompt = Processors::get_opt_variant<Processors::SelectYesNo>(field.core.subunits.back());
	expect(intercept_prompt && intercept_prompt->playerid == 3,
		"the first Let me take it prompt must be routed to Tristan's logical seat");
	field.returns.set<int32_t>(0, 1);
	effect_damage.step = 20;
	expect(!field.process(effect_damage) && effect_damage.duelist == 1,
		"accepting Let me take it must redirect effect damage to Tristan's life points");
	field.core.subunits.clear();
	const auto serenity_lp = field.get_logical_lp(0, 0);
	Processors::Damage batch_damage(0, nullptr, REASON_EFFECT, 1, nezbitt, 0, 100, false, 0, false);
	expect(!field.process(batch_damage), "non-interceptable batch damage must resolve its first processing step");
	expect(field.core.subunits.empty(),
		"non-interceptable batch damage must not enqueue a Let me take it prompt");
	batch_damage.step = 1;
	expect(!field.process(batch_damage), "non-interceptable batch damage must apply to life points");
	expect(field.get_logical_lp(0, 0) == serenity_lp - 100,
		"non-interceptable batch damage must affect the selected logical player");

	OCG_DuelOptions royale_options = options;
	royale_options.flags = DUEL_BATTLE_ROYALE;
	bool royale_valid_lua = true;
	duel royale(royale_options, royale_valid_lua);
	expect(royale_valid_lua, "the Battle Royale Lua runtime must initialize");
	auto& royale_field = *royale.game_field;
	expect(royale_field.player[0].list_mzone.size() == 14
			&& royale_field.player[1].list_mzone.size() == 14,
		"Battle Royale must allocate two monster fields on both core sides");
	expect(royale_field.player[0].list_szone.size() == 16
			&& royale_field.player[1].list_szone.size() == 16,
		"Battle Royale must allocate two spell/trap fields on both core sides");
	initialize_extra_duelist(royale, 1, 4001);
	const OCG_NewCardInfo side1_extra{
		1,
		1,
		4002,
		0,
		LOCATION_DECK,
		0,
		POS_FACEDOWN_DEFENSE
	};
	OCG_DuelNewCard(&royale, &side1_extra);

	auto* kaiba = royale.new_card(4100);
	kaiba->owner = 0;
	kaiba->owner_duelist = 0;
	royale_field.add_card(0, kaiba, LOCATION_MZONE, 0, false, 0);
	auto* yugi = royale.new_card(4101);
	yugi->owner = 0;
	yugi->owner_duelist = 1;
	expect(royale_field.tag_swap_to(0, 1), "side 0 must switch to Yugi before he uses his field");
	royale_field.add_card(0, yugi, LOCATION_MZONE, 0);
	auto* marik = royale.new_card(4102);
	marik->owner = 1;
	marik->owner_duelist = 0;
	royale_field.add_card(1, marik, LOCATION_MZONE, 0, false, 0);
	auto* joey = royale.new_card(4103);
	joey->owner = 1;
	joey->owner_duelist = 1;
	expect(royale_field.tag_swap_to(1, 1), "side 1 must switch to Joey before he uses his field");
	royale_field.add_card(1, joey, LOCATION_MZONE, 0);
	expect(kaiba->current.sequence == 0 && yugi->current.sequence == 7
			&& marik->current.sequence == 0 && joey->current.sequence == 7,
		"all four Battle Royale players must keep zone 0 on their own saved field");
	expect(royale_field.player[0].list_mzone[0] == kaiba
			&& royale_field.player[0].list_mzone[7] == yugi
			&& royale_field.player[1].list_mzone[0] == marik
			&& royale_field.player[1].list_mzone[7] == joey,
		"switching turns must not merge a Battle Royale player's field with another player");
	auto* yugi_effect = royale.new_effect();
	yugi_effect->owner = yugi;
	yugi_effect->handler = yugi;
	royale_field.core.reason_effect = yugi_effect;
	expect(royale_field.get_response_player(0) == 3,
		"an effect on Yugi's saved field must prompt Yugi rather than Kaiba");
	auto* joey_effect = royale.new_effect();
	joey_effect->owner = joey;
	joey_effect->handler = joey;
	royale_field.core.reason_effect = joey_effect;
	expect(royale_field.get_response_player(1) == 5,
		"an effect on Joey's saved field must prompt Joey rather than Marik");
	royale_field.core.reason_effect = nullptr;
	expect(royale_field.tag_swap_to(0, 0), "the visible side-0 resources must switch back to Kaiba");
	royale_field.core.attacker = kaiba;
	royale_field.core.attack_target_logical = 1;
	card_vector royale_targets;
	royale_field.get_attack_target(kaiba, &royale_targets);
	expect(royale_targets.size() == 1 && royale_targets.front() == yugi,
		"Kaiba must be able to select Yugi even though both share core side 0");
	royale_field.core.attack_target_logical = 3;
	royale_targets.clear();
	royale_field.get_attack_target(kaiba, &royale_targets);
	expect(royale_targets.size() == 1 && royale_targets.front() == joey,
		"Kaiba must be able to switch the projected opponent field to Joey");
	royale_field.get_logical_lp(0, 0) = 4000;
	royale_field.get_logical_lp(0, 1) = 3000;
	royale_field.get_logical_lp(1, 0) = 2000;
	royale_field.get_logical_lp(1, 1) = 1000;
	expect(royale_field.get_logical_lp(0, 0) == 4000
				&& royale_field.get_logical_lp(0, 1) == 3000
				&& royale_field.get_logical_lp(1, 0) == 2000
				&& royale_field.get_logical_lp(1, 1) == 1000,
			"Battle Royale life points must remain independent for all four players");
	royale_field.core.subunits.clear();
	royale_field.infos.turn_player = 0;
	royale_field.core.attacker = kaiba;
	royale_field.core.attack_target_logical = 2;
	royale_field.core.attack_target_duelist = 0;
	Processors::BattleCommand royale_attack(4);
	expect(!royale_field.process(royale_attack),
		"Battle Royale attack selection must pause for Let me take it");
	auto* royale_attack_prompt =
		Processors::get_opt_variant<Processors::SelectYesNo>(royale_field.core.subunits.back());
	expect(royale_attack_prompt && royale_attack_prompt->playerid == 3,
		"Battle Royale attack interception must prompt the first eligible logical seat");
	royale_field.returns.set<int32_t>(0, 1);
	royale_attack.step = 44;
	expect(!royale_field.process(royale_attack)
			&& royale_field.core.attack_target_logical == 1
			&& royale_field.core.attack_target_duelist == 1,
		"Let me take it must redirect the attack to the accepting Battle Royale player");
	royale_field.core.subunits.clear();
	Processors::Damage royale_effect_damage(
		0, nullptr, REASON_EFFECT, 0, kaiba, 1, 600, false, 0, true);
	expect(!royale_field.process(royale_effect_damage),
		"Battle Royale effect damage must pause for Let me take it");
	auto* royale_intercept_prompt =
		Processors::get_opt_variant<Processors::SelectYesNo>(royale_field.core.subunits.back());
	expect(royale_intercept_prompt && royale_intercept_prompt->playerid == 3,
		"Battle Royale must offer interception to the first eligible logical player");
	royale_field.returns.set<int32_t>(0, 1);
	royale_effect_damage.step = 20;
	expect(!royale_field.process(royale_effect_damage)
			&& royale_effect_damage.playerid == 0
			&& royale_effect_damage.duelist == 1,
		"Battle Royale interception must redirect damage to the accepting player's own LP");
	const auto yugi_lp = royale_field.get_logical_lp(0, 1);
	royale_effect_damage.step = 0;
	expect(!royale_field.process(royale_effect_damage),
		"redirected Battle Royale damage must resume without another prompt");
	royale_effect_damage.step = 1;
	expect(!royale_field.process(royale_effect_damage)
			&& royale_field.get_logical_lp(0, 1) == yugi_lp - 600,
		"accepted Battle Royale interception must reduce only the interceptor's LP");
	expect(royale_field.tag_swap_to(0, 1), "the visible side-0 resources must switch to Yugi again");
	expect(royale_field.player[0].list_mzone[0] == kaiba
			&& royale_field.player[0].list_mzone[7] == yugi,
		"a Battle Royale tag swap must preserve both saved monster fields");

	std::cout << "All multiplayer field tests passed.\n";
	return 0;
}
