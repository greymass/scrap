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

#ifdef DEBUG
void token::destroy(const name& issuer, const asset& maximum_supply)
{
   require_auth(get_self());

   auto sym = maximum_supply.symbol;
   check(maximum_supply.is_valid(), "invalid supply");
   check(maximum_supply.amount > 0, "max-supply must be positive");

   stats statstable(get_self(), sym.code().raw());
   auto  existing = statstable.find(sym.code().raw());
   check(existing != statstable.end(), "token does not exist");

   statstable.erase(existing);

   accounts accountstable(get_self(), "wharfkittest"_n.value);
   auto     itr = accountstable.begin();
   while (itr != accountstable.end()) {
      itr = accountstable.erase(itr);
   }
}
#endif

[[eosio::on_notify("drops::logdestroy")]] void token::mint(const name                                 owner,
                                                           const vector<dropssystem::drops::drop_row> droplet_ids,
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

   // Derive the start time of current epoch, all destroyed Droplets must have been created before this
   const block_timestamp valid_before =
      dropssystem::epoch::derive_epoch_start(state.genesis, state.duration, epoch_height);

   // Load the usable/previous epoch from the table
   auto epoch = _epoch.find(epoch_previous);

   // Ensure the current epoch has been revealed
   check(epoch->seed != checksum256{}, "Waiting for the previous epoch to be revealed by oracles.");

   // The current token supply
   stats statstable(get_self(), SCRAP_SYMBOL.code().raw());
   auto  existing = statstable.find(SCRAP_SYMBOL.code().raw());
   check(existing != statstable.end(), "token with symbol does not exist, create token before issue");
   const auto& st = *existing;

   // Running total of the supply as it increases during this transaction
   uint64_t current_supply = st.supply.amount;

   // The amount of SCRAP to for the given Droplet(s) destroyed (in units)
   uint64_t amount = 0;

   // The result of the mint process
   vector<mint_result> results;

   // Compute the hash for the provided Droplet(s) using the previous epoch revealed seed
   for (auto itr = begin(droplet_ids); itr != end(droplet_ids); ++itr) {
      check(itr->created < valid_before,
            "An included Drop was created (" + std::to_string(itr->created.to_time_point().sec_since_epoch()) +
               ") after the start (" + std::to_string(valid_before.to_time_point().sec_since_epoch()) +
               ") of the valid epoch (" + std::to_string(epoch_previous) + ").");

      // Combine epoch seed value and Droplet seed value to create a unique hash
      const checksum256 hash = dropssystem::epoch::hashdrop(epoch->seed, itr->seed);

      // Convert to a hex value and count the leading zeros
      auto   hash_array  = hash.extract_as_byte_array();
      string hash_result = dropssystem::epoch::hex_to_str(hash_array.data(), hash_array.size());
      auto   zeros       = dropssystem::epoch::clzhex(hash_result);

      // Ensure the leading zeros meet the difficulty requirement
      check(zeros >= SCRAP_MINING_DIFFICULTY,
            "Hash (" + hash_result + ") for provided Droplet (" + std::to_string(itr->seed) +
               ")  does not meet the difficulty requirement of " + std::to_string(SCRAP_MINING_DIFFICULTY) + " (" +
               std::to_string(zeros) + ").");

      // Save a reciept of this Drop being minted into SCRAP
      results.push_back(mint_result{itr->seed, hash});

      // The amount of SCRAP to receive for this Droplet
      const uint64_t mint_amount = get_mint_amount(current_supply);

      // Add to the total amount the participant will receive
      amount += mint_amount;

      // Add to the current supply while iterating over Droplets
      current_supply += mint_amount;
   }

   asset quantity = asset(amount, SCRAP_SYMBOL);
   statstable.modify(st, get_self(), [&](auto& s) { s.supply += quantity; });

   add_balance(owner, quantity, get_self());

   token::logmint_action logmint{get_self(), {get_self(), "active"_n}};
   logmint.send(owner, quantity, epoch_previous, epoch->seed, results);
}

uint64_t token::get_mint_amount(const uint64_t current_supply)
{
   // Constants for use in the minting process
   const uint64_t units   = 1;
   const uint64_t million = 1'000'000;
   // The first era ends when 100 million tokens are minted
   const uint64_t end_first_era = 100 * million * units;
   // The second era ends when 300 million tokens are minted
   const uint64_t end_second_era = 300 * million * units;
   // The third era ends when 600 million tokens are minted
   const uint64_t end_third_era = 600 * million * units;
   // The fourth era and final era ends when all 1000 million tokens are minted
   const uint64_t end_fourth_era = 1000 * million * units;
   if (current_supply < end_first_era) {
      // First era receives 8 tokens per Droplet
      return 8 * units;
   } else if (current_supply < end_second_era) {
      // Second era receives 4 tokens per Droplet
      return 4 * units;
   } else if (current_supply < end_third_era) {
      // Third era receives 2 tokens per Droplet
      return 2 * units;
   } else if (current_supply < end_fourth_era) {
      // Fourth era receives 1 tokens per Droplet
      return 1 * units;
   } else {
      // If the maximum supply is reached, 0 tokens are minted
      return 0;
   }
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
