#include <algorithm>
#include "card.h"
#include "duel.h"
#include "field.h"

bool field::process(Processors::BattleRoyaleTurnOrder& arg) {
    if(multiplayer.mode() != MultiplayerMode::BATTLE_ROYALE)
        return TRUE;
    switch(arg.step) {
    case 0: {
        while(arg.logical_player < MultiplayerState::MAX_PLAYERS
                && !multiplayer.is_active(arg.logical_player))
            ++arg.logical_player;
        if(arg.logical_player >= MultiplayerState::MAX_PLAYERS) {
            multiplayer.set_turn_order_by_attack(arg.selected_attack);
            return TRUE;
        }
        const auto side = multiplayer.field_side_of(arg.logical_player);
        const auto duelist = multiplayer.duelist_index_of(arg.logical_player);
        auto& deck = get_logical_list(side, LOCATION_DECK, duelist);
        core.select_cards.clear();
        for(auto* pcard : deck) {
            if(pcard && (pcard->data.type & TYPE_MONSTER))
                core.select_cards.push_back(pcard);
        }
        if(core.select_cards.empty()) {
            arg.selected_cards[arg.logical_player] = nullptr;
            arg.selected_attack[arg.logical_player] = 0;
            ++arg.logical_player;
            arg.step = Processors::restart;
            return FALSE;
        }
        emplace_process<Processors::SelectCard>(side, false, 1, 1,
            arg.logical_player);
        return FALSE;
    }
    case 1: {
        const auto logical = arg.logical_player;
        if(return_cards.list.size() == 1 && return_cards.list.front()) {
            auto* selected = return_cards.list.front();
            arg.selected_cards[logical] = selected;
            arg.selected_attack[logical] = std::max<int32_t>(0, selected->data.attack);
        } else {
            arg.selected_cards[logical] = nullptr;
            arg.selected_attack[logical] = 0;
        }
        ++arg.logical_player;
        if(arg.logical_player < MultiplayerState::MAX_PLAYERS) {
            arg.step = Processors::restart;
            return FALSE;
        }
        for(uint8_t player = 0; player < MultiplayerState::MAX_PLAYERS; ++player) {
            auto* selected = arg.selected_cards[player];
            if(!selected)
                continue;
            const auto side = multiplayer.field_side_of(player);
            const auto duelist = multiplayer.duelist_index_of(player);
            const auto from = selected->get_info_location();
            selected->current.reason = REASON_RULE;
            selected->current.reason_effect = nullptr;
            selected->current.reason_player = PLAYER_NONE;
            remove_card(selected);
            selected->sendto_param.position = POS_FACEUP;
            selected->current.position = POS_FACEUP;
            add_card(side, selected, LOCATION_REMOVED, 0, false, duelist);
            auto message = pduel->new_message(MSG_MOVE);
            message->write<uint32_t>(selected->data.code);
            message->write(from);
            message->write(selected->get_info_location());
            message->write<uint32_t>(REASON_RULE);
        }
        multiplayer.set_turn_order_by_attack(arg.selected_attack);
        publish_all_multiplayer_private_piles();
        return TRUE;
    }
    }
    return TRUE;
}
