#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>

using namespace eosio;

class [[eosio::contract("gamestub")]] gamestub : public contract
{
public:
   using contract::contract;

   [[eosio::action]] void sendcommit(const name token, const uint32_t closed, const uint32_t opened, const asset amount)
   {
      action({get_self(), "active"_n}, token, "commit"_n, std::make_tuple(closed, opened, amount)).send();
   }

   struct [[eosio::table("emitcfg")]] emitcfg_row
   {
      name token_contract;
   };

   [[eosio::action]] void setemitcfg(const name token_contract)
   {
      eosio::singleton<"emitcfg"_n, emitcfg_row>(get_self(), get_self().value).set(emitcfg_row{token_contract}, get_self());
   }

   struct [[eosio::table("games")]] game_row
   {
      name           contract;
      checksum256    seed;
      uint32_t       epochtime;
      time_point_sec start;
      time_point_sec end;
      uint64_t       primary_key() const { return contract.value; }
   };

   [[eosio::action]] void setgame(const name contract, const uint32_t epochtime, const time_point_sec start)
   {
      eosio::multi_index<"games"_n, game_row> games(get_self(), get_self().value);
      const auto                              row = games.find(contract.value);
      if (row == games.end()) {
         games.emplace(get_self(), [&](auto& g) {
            g.contract  = contract;
            g.epochtime = epochtime;
            g.start     = start;
         });
      } else {
         games.modify(row, get_self(), [&](auto& g) {
            g.epochtime = epochtime;
            g.start     = start;
         });
      }
   }

   [[eosio::action]] void sendmint(const name token, const name player, const asset quantity)
   {
      action({get_self(), "active"_n}, token, "mintshare"_n, std::make_tuple(player, quantity)).send();
   }
};
