#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>

using namespace eosio;

class [[eosio::contract("gamestub")]] gamestub : public contract
{
public:
   using contract::contract;

   [[eosio::action]] void sendcommit(const name token, const uint32_t closed, const uint32_t opened, const asset amount)
   {
      action({get_self(), "active"_n}, token, "commit"_n, std::make_tuple(closed, opened, amount)).send();
   }

   [[eosio::action]] void sendmint(const name token, const name player, const asset quantity)
   {
      action({get_self(), "active"_n}, token, "mintshare"_n, std::make_tuple(player, quantity)).send();
   }
};
