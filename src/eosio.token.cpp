#include <eosio.token/eosio.token.hpp>

namespace eosio {

void token::create(const name& issuer, const asset& maximum_supply)
{
   require_auth(get_self());

   auto sym = maximum_supply.symbol;
   check(maximum_supply.is_valid(), "invalid supply");
   check(maximum_supply.amount > 0, "max-supply must be positive");

   stats statstable(get_self(), sym.code().raw());
   auto  existing = statstable.find(sym.code().raw());
   check(existing == statstable.end(), "token with symbol already exists");

   statstable.emplace(get_self(), [&](auto& s) {
      s.supply.symbol = maximum_supply.symbol;
      s.max_supply    = maximum_supply;
      s.issuer        = issuer;
   });
}

[[eosio::on_notify("drops::logdestroy")]] void token::mint(const name                                 owner,
                                                           const vector<dropssystem::drops::drop_row> drops,
                                                           const int64_t                              destroyed,
                                                           const int64_t                              unbound_destroyed,
                                                           const int64_t                              bytes_reclaimed,
                                                           optional<string>                           memo,
                                                           optional<name>                             to_notify)
{
   const dropssystem::epoch::epoch_table _epoch("epoch.drops"_n, "epoch.drops"_n.value);
   dropssystem::epoch::state_table       _state("epoch.drops"_n, "epoch.drops"_n.value);

   // Retrieve the current epoch being used (current - 1)
   auto     state          = _state.get();
   uint64_t epoch_height   = dropssystem::epoch::derive_epoch(state.genesis, state.duration);
   uint64_t epoch_previous = epoch_height - 1;

   // Derive the start time of current epoch, all destroyed drops must have been created before this
   const block_timestamp valid_before =
      dropssystem::epoch::derive_epoch_start(state.genesis, state.duration, epoch_height);

   // Load the usable/previous epoch from the table
   auto epoch = _epoch.find(epoch_previous);

   // Ensure the current epoch has been revealed
   check(epoch->seed != checksum256{}, "Waiting for the previous epoch to be revealed by oracles.");

   // The mining difficult (number of leading zeros)
   uint32_t difficulty = 2;

   // The amount to issue per Drop destroyed (in units)
   uint64_t amount = 10000;

   // The result of the mint process
   vector<mint_result> results;

   // Compute the hash for the provided drop(s) using the previous epoch revealed seed
   for (auto itr = begin(drops); itr != end(drops); ++itr) {
      check(itr->created < valid_before,
            "An included Drop was created (" + std::to_string(itr->created.to_time_point().sec_since_epoch()) +
               ") after the start (" + std::to_string(valid_before.to_time_point().sec_since_epoch()) +
               ") of the valid epoch (" + std::to_string(epoch_previous) + ").");

      const checksum256 hash = dropssystem::epoch::hashdrop(epoch->seed, itr->seed);

      auto   hash_array  = hash.extract_as_byte_array();
      string hash_result = dropssystem::epoch::hex_to_str(hash_array.data(), hash_array.size());

      // Count the leading zeros of the hex value
      auto zeros = dropssystem::epoch::clzbinary(hash_array);

      check(zeros >= difficulty, "Hash (" + hash_result + ") for provided drop (" + std::to_string(itr->seed) +
                                    ")  does not meet the difficulty requirement of " + std::to_string(difficulty) +
                                    " (" + std::to_string(zeros) + ").");

      results.push_back(mint_result{itr->seed, hash});
   }

   symbol  token_symbol = symbol{"DEMO", 4};
   int64_t total        = drops.size() * amount;
   asset   quantity     = asset{total, token_symbol};

   stats statstable(get_self(), token_symbol.code().raw());
   auto  existing = statstable.find(token_symbol.code().raw());
   check(existing != statstable.end(), "token with symbol does not exist, create token before mint");
   const auto& st = *existing;

   statstable.modify(st, get_self(), [&](auto& s) { s.supply += quantity; });

   add_balance(owner, quantity, get_self());

   token::logmint_action logmint{get_self(), {get_self(), "active"_n}};
   logmint.send(owner, quantity, epoch_previous, epoch->seed, results);
}

[[eosio::action]] void token::logmint(
   const name owner, const asset minted, const uint64_t epoch, const checksum256 seed, vector<mint_result> result)
{
   require_auth(get_self());
   if (owner != get_self()) {
      require_recipient(owner);
   }
}

void token::issue(const name& to, const asset& quantity, const string& memo)
{
   auto sym = quantity.symbol;
   check(sym.is_valid(), "invalid symbol name");
   check(memo.size() <= 256, "memo has more than 256 bytes");

   stats statstable(get_self(), sym.code().raw());
   auto  existing = statstable.find(sym.code().raw());
   check(existing != statstable.end(), "token with symbol does not exist, create token before issue");
   const auto& st = *existing;
   check(to == st.issuer, "tokens can only be issued to issuer account");

   require_auth(st.issuer);
   check(quantity.is_valid(), "invalid quantity");
   check(quantity.amount > 0, "must issue positive quantity");

   check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
   check(quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

   statstable.modify(st, same_payer, [&](auto& s) { s.supply += quantity; });

   add_balance(st.issuer, quantity, st.issuer);
}

void token::retire(const asset& quantity, const string& memo)
{
   auto sym = quantity.symbol;
   check(sym.is_valid(), "invalid symbol name");
   check(memo.size() <= 256, "memo has more than 256 bytes");

   stats statstable(get_self(), sym.code().raw());
   auto  existing = statstable.find(sym.code().raw());
   check(existing != statstable.end(), "token with symbol does not exist");
   const auto& st = *existing;

   require_auth(st.issuer);
   check(quantity.is_valid(), "invalid quantity");
   check(quantity.amount > 0, "must retire positive quantity");

   check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");

   statstable.modify(st, same_payer, [&](auto& s) { s.supply -= quantity; });

   sub_balance(st.issuer, quantity);
}

void token::transfer(const name& from, const name& to, const asset& quantity, const string& memo)
{
   check(from != to, "cannot transfer to self");
   require_auth(from);
   check(is_account(to), "to account does not exist");
   auto        sym = quantity.symbol.code();
   stats       statstable(get_self(), sym.raw());
   const auto& st = statstable.get(sym.raw());

   require_recipient(from);
   require_recipient(to);

   check(quantity.is_valid(), "invalid quantity");
   check(quantity.amount > 0, "must transfer positive quantity");
   check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
   check(memo.size() <= 256, "memo has more than 256 bytes");

   auto payer = has_auth(to) ? to : from;

   sub_balance(from, quantity);
   add_balance(to, quantity, payer);
}

void token::sub_balance(const name& owner, const asset& value)
{
   accounts from_acnts(get_self(), owner.value);

   const auto& from = from_acnts.get(value.symbol.code().raw(), "no balance object found");
   check(from.balance.amount >= value.amount, "overdrawn balance");

   from_acnts.modify(from, owner, [&](auto& a) { a.balance -= value; });
}

void token::add_balance(const name& owner, const asset& value, const name& ram_payer)
{
   accounts to_acnts(get_self(), owner.value);
   auto     to = to_acnts.find(value.symbol.code().raw());
   if (to == to_acnts.end()) {
      to_acnts.emplace(ram_payer, [&](auto& a) { a.balance = value; });
   } else {
      to_acnts.modify(to, same_payer, [&](auto& a) { a.balance += value; });
   }
}

void token::open(const name& owner, const symbol& symbol, const name& ram_payer)
{
   require_auth(ram_payer);

   check(is_account(owner), "owner account does not exist");

   auto        sym_code_raw = symbol.code().raw();
   stats       statstable(get_self(), sym_code_raw);
   const auto& st = statstable.get(sym_code_raw, "symbol does not exist");
   check(st.supply.symbol == symbol, "symbol precision mismatch");

   accounts acnts(get_self(), owner.value);
   auto     it = acnts.find(sym_code_raw);
   if (it == acnts.end()) {
      acnts.emplace(ram_payer, [&](auto& a) { a.balance = asset{0, symbol}; });
   }
}

void token::close(const name& owner, const symbol& symbol)
{
   require_auth(owner);
   accounts acnts(get_self(), owner.value);
   auto     it = acnts.find(symbol.code().raw());
   check(it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect.");
   check(it->balance.amount == 0, "Cannot close because the balance is not zero.");
   acnts.erase(it);
}

} // namespace eosio
