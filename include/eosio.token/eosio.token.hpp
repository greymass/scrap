#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <epoch.drops/epoch.drops.hpp>

#include <initializer_list>
#include <string>

namespace eosiosystem {
class system_contract;
}

namespace eosio {

using dropssystem::drops;
using dropssystem::epoch;
using std::string;

/**
 * The `eosio.token` sample system contract defines the structures and actions that allow users to create, issue, and
 * manage tokens for EOSIO based blockchains. It demonstrates one way to implement a smart contract which allows for
 * creation and management of tokens. It is possible for one to create a similar contract which suits different needs.
 * However, it is recommended that if one only needs a token with the below listed actions, that one uses the
 * `eosio.token` contract instead of developing their own.
 *
 * The `eosio.token` contract class also implements two useful public static methods: `get_supply` and `get_balance`.
 * The first allows one to check the total supply of a specified token, created by an account and the second allows one
 * to check the balance of a token for a specified account (the token creator account has to be specified as well).
 *
 * The `eosio.token` contract manages the set of tokens, accounts and their corresponding balances, by using two
 * internal multi-index structures: the `accounts` and `stats`. The `accounts` multi-index table holds, for each row,
 * instances of `account` object and the `account` object holds information about the balance of one token. The
 * `accounts` table is scoped to an EOSIO account, and it keeps the rows indexed based on the token's symbol.  This
 * means that when one queries the `accounts` multi-index table for an account name the result is all the tokens that
 * account holds at the moment.
 *
 * Similarly, the `stats` multi-index table, holds instances of `currency_stats` objects for each row, which contains
 * information about current supply, maximum supply, and the creator account for a symbol token. The `stats` table is
 * scoped to the token symbol.  Therefore, when one queries the `stats` table for a token symbol the result is one
 * single entry/row corresponding to the queried symbol token if it was previously created, or nothing, otherwise.
 */
class [[eosio::contract("eosio.token")]] token : public contract
{
public:
   using contract::contract;

   // The token symbol for SCRAP
   const symbol SCRAP_SYMBOL = symbol{"SCRAP", 0};

   // The mining difficult of the hash hex value (number of leading zeros)
   // 2 difficulty = 16 * 16 = 1:256 odds
   const uint32_t SCRAP_MINING_DIFFICULTY = 2;

   /**
    * Allows `issuer` account to create a token in supply of `maximum_supply`. If validation is successful a new
    * entry in statstable for token symbol scope gets created.
    *
    * @param issuer - the account that creates the token,
    * @param maximum_supply - the maximum supply set for the token created.
    *
    * @pre Token symbol has to be valid,
    * @pre Token symbol must not be already created,
    * @pre maximum_supply has to be smaller than the maximum supply allowed by the system: 1^62 - 1.
    * @pre Maximum supply must be positive;
    */
   [[eosio::action]] void create(const name& issuer, const asset& maximum_supply);

   /**
    * This action mint new tokens to the `owner` account, provided the `droplet_ids` destroyed are valid and `memo` meet
    * the criteria of minting.
    *
    * @param owner - the account that creates the token,
    * @param drops - the maximum supply set for the token created.
    * @param memo - the memo string to accompany the transaction.
    */
   [[eosio::on_notify("drops::logdestroy")]] void mint(const name                                 owner,
                                                       const vector<dropssystem::drops::drop_row> droplet_ids,
                                                       const int64_t                              destroyed,
                                                       const int64_t                              unbound_destroyed,
                                                       const int64_t                              bytes_reclaimed,
                                                       optional<string>                           memo,
                                                       optional<name>                             to_notify);

   /**
    * A struct that represents the computed result of the hashing that took place during the minting process.
    */
   struct mint_result
   {
      uint64_t    seed;
      checksum256 hash;
   };

   /**
    * This action logs the minting of tokens to the `owner` account.
    */
   [[eosio::action]] void logmint(const name                owner,
                                  const asset               minted,
                                  const uint64_t            epoch,
                                  const checksum256         hash,
                                  const vector<mint_result> results);

   /**
    *  This action issues to `to` account a `quantity` of tokens.
    *
    * @param to - the account to issue tokens to, it must be the same as the issuer,
    * @param quantity - the amount of tokens to be issued,
    * @memo - the memo string that accompanies the token issue transaction.
    */
   [[eosio::action]] void issue(const name& to, const asset& quantity, const string& memo);

   /**
    * The opposite for create action, if all validations succeed,
    * it debits the statstable.supply amount.
    *
    * @param quantity - the quantity of tokens to retire,
    * @param memo - the memo string to accompany the transaction.
    */
   [[eosio::action]] void retire(const asset& quantity, const string& memo);

   /**
    * Allows `from` account to transfer to `to` account the `quantity` tokens.
    * One account is debited and the other is credited with quantity tokens.
    *
    * @param from - the account to transfer from,
    * @param to - the account to be transferred to,
    * @param quantity - the quantity of tokens to be transferred,
    * @param memo - the memo string to accompany the transaction.
    */
   [[eosio::action]] void transfer(const name& from, const name& to, const asset& quantity, const string& memo);
   /**
    * Allows `ram_payer` to create an account `owner` with zero balance for
    * token `symbol` at the expense of `ram_payer`.
    *
    * @param owner - the account to be created,
    * @param symbol - the token to be payed with by `ram_payer`,
    * @param ram_payer - the account that supports the cost of this action.
    *
    * More information can be read [here](https://github.com/EOSIO/eosio.contracts/issues/62)
    * and [here](https://github.com/EOSIO/eosio.contracts/issues/61).
    */
   [[eosio::action]] void open(const name& owner, const symbol& symbol, const name& ram_payer);

   /**
    * This action is the opposite for open, it closes the account `owner`
    * for token `symbol`.
    *
    * @param owner - the owner account to execute the close action for,
    * @param symbol - the symbol of the token to execute the close action for.
    *
    * @pre The pair of owner plus symbol has to exist otherwise no action is executed,
    * @pre If the pair of owner plus symbol exists, the balance has to be zero.
    */
   [[eosio::action]] void close(const name& owner, const symbol& symbol);

   struct support
   {
      name  account;
      asset weight;
   };

   [[eosio::action]] void init(const name           eon,
                               const name           nex,
                               const time_point_sec epoch_start,
                               const uint32_t       epoch_seconds,
                               const uint32_t       notice_epochs);

   [[eosio::action]] void approve(const name holder);

   [[eosio::action]] void withdraw(const name holder);

   [[eosio::action]] void closevote(const uint32_t max_rows);

   [[eosio::action]] void logratify(const asset          tally,
                                    const asset          supply,
                                    const uint32_t       ratified_epoch,
                                    const uint32_t       start_epoch,
                                    const time_point_sec start_time);

   [[eosio::action]] void logsupport(const vector<support> supporters, const bool closed);

   struct curve_band
   {
      asset below;
      asset per_epoch;
   };

   [[eosio::action]] void setcurve(const vector<curve_band> bands, const uint32_t ramp_epochs);

   [[eosio::action]] void commit(const uint32_t closed_epoch, const uint32_t opened_epoch, const asset amount);

   [[eosio::action]] void logtarget(const uint32_t closed_epoch, const uint32_t opened_epoch, const asset target);

   [[eosio::action]] void mintshare(const name player, const asset quantity);

#ifdef DEBUG
   /**
    * FOR DEBUGGING: This action will destroy all balances and reset the contract.
    */
   [[eosio::action]] void destroy(const name& issuer, const asset& maximum_supply);
#endif

   static asset get_supply(const name& token_contract_account, const symbol_code& sym_code)
   {
      stats       statstable(token_contract_account, sym_code.raw());
      const auto& st = statstable.get(sym_code.raw(), "invalid supply symbol code");
      return st.supply;
   }

   static asset get_balance(const name& token_contract_account, const name& owner, const symbol_code& sym_code)
   {
      accounts    accountstable(token_contract_account, owner.value);
      const auto& ac = accountstable.get(sym_code.raw(), "no balance with specified symbol");
      return ac.balance;
   }

   using create_action   = eosio::action_wrapper<"create"_n, &token::create>;
   using issue_action    = eosio::action_wrapper<"issue"_n, &token::issue>;
   using retire_action   = eosio::action_wrapper<"retire"_n, &token::retire>;
   using transfer_action = eosio::action_wrapper<"transfer"_n, &token::transfer>;
   using open_action     = eosio::action_wrapper<"open"_n, &token::open>;
   using close_action    = eosio::action_wrapper<"close"_n, &token::close>;
   using logmint_action  = eosio::action_wrapper<"logmint"_n, &token::logmint>;

   using init_action       = eosio::action_wrapper<"init"_n, &token::init>;
   using approve_action    = eosio::action_wrapper<"approve"_n, &token::approve>;
   using withdraw_action   = eosio::action_wrapper<"withdraw"_n, &token::withdraw>;
   using closevote_action  = eosio::action_wrapper<"closevote"_n, &token::closevote>;
   using logratify_action  = eosio::action_wrapper<"logratify"_n, &token::logratify>;
   using logsupport_action = eosio::action_wrapper<"logsupport"_n, &token::logsupport>;
   using setcurve_action   = eosio::action_wrapper<"setcurve"_n, &token::setcurve>;
   using commit_action     = eosio::action_wrapper<"commit"_n, &token::commit>;
   using logtarget_action  = eosio::action_wrapper<"logtarget"_n, &token::logtarget>;
   using mintshare_action  = eosio::action_wrapper<"mintshare"_n, &token::mintshare>;

private:
   struct [[eosio::table]] account
   {
      asset balance;

      uint64_t primary_key() const { return balance.symbol.code().raw(); }
   };

   struct [[eosio::table]] currency_stats
   {
      asset supply;
      asset max_supply;
      name  issuer;

      uint64_t primary_key() const { return supply.symbol.code().raw(); }
   };

   typedef eosio::multi_index<"accounts"_n, account>    accounts;
   typedef eosio::multi_index<"stat"_n, currency_stats> stats;

   struct [[eosio::table("config")]] config_row
   {
      name           eon;
      name           nex;
      time_point_sec epoch_start;
      uint32_t       epoch_seconds = 0;
      uint32_t       notice_epochs = 0;
   };

   struct [[eosio::table("vote")]] vote_row
   {
      asset          tally;
      bool           ratified = false;
      bool           closed   = false;
      asset          supply;
      uint32_t       ratified_epoch = 0;
      uint32_t       start_epoch    = 0;
      time_point_sec start_time;
   };

   struct [[eosio::table("approvals")]] approval_row
   {
      name  account;
      asset snapshot;
      bool  snapped = false;

      uint64_t primary_key() const { return account.value; }
   };

   struct [[eosio::table("curve")]] curve_row
   {
      vector<curve_band> bands;
      uint32_t           ramp_epochs = 0;
   };

   struct [[eosio::table("emission")]] emission_row
   {
      asset    owed;
      uint32_t committed_epoch = 0;
      asset    committed;
      uint32_t target_epoch = 0;
      asset    target;
   };

   typedef eosio::singleton<"config"_n, config_row>        config_table;
   typedef eosio::singleton<"vote"_n, vote_row>            vote_table;
   typedef eosio::multi_index<"approvals"_n, approval_row> approvals_table;
   typedef eosio::singleton<"curve"_n, curve_row>          curve_table;
   typedef eosio::singleton<"emission"_n, emission_row>    emission_table;

   struct balance_change
   {
      name    owner;
      int64_t before;
      int64_t after;
   };

   int64_t  sub_balance(const name& owner, const asset& value);
   int64_t  add_balance(const name& owner, const asset& value, const name& ram_payer);
   uint64_t get_mint_amount(const uint64_t current_supply);

   int64_t  balance_of(const name& owner);
   uint32_t current_epoch(const config_row& config);
   bool     handover_started();
   bool     try_ratify(vote_row& vote);
   void     track_vote(std::initializer_list<balance_change> changes, const bool supply_changed);

   void    check_curve(const vector<curve_band>& bands, const uint32_t ramp_epochs);
   int64_t owed_amount();
   int64_t epoch_target(const vote_row& vote, const uint32_t epoch, const int64_t owed);
};

} // namespace eosio
